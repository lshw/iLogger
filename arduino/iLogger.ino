#include <EEPROM.h>
#include <LiquidCrystal.h>
#include <MsTimer2.h>
#include <avr/sleep.h>
#include <SdFat.h>
SdFat sd;
SdFile myFile;
#define chipSelect  10

int led = 13;
#define VBAT A0 //电源电压
#define VOUT A1  //LOW-打开输出
#define R3 A4   //LOW 采样电阻加3.3欧
#define R33 A3  //LOW 采样电阻加33欧
#define R333 2  //LOW 采样电阻加330欧
#define I333 A7  //330欧的AD
#define I33 A5  //33欧的AD
#define I03 A2 //0.33欧的AD
#define LAMP 9  //LCD背光

#include <SdFat.h>
boolean have_sd=false;

uint16_t file_no;

LiquidCrystal lcd(8, 7, 6, 5, 4, 3); //rs,en,d4,d5,d6,d7
uint16_t i0;
uint8_t h=0,m=0,s=0;
uint16_t ms=0;
boolean m_save; //住循环需要保存当前分钟电流
boolean r3,r33,r333; //存放采样电阻切换值， r3==LOW +3.3欧，  r33==LOW,+33欧
uint16_t  v,i_error=0;  //v->电池电压mv，电流大于2A，报警倒计数（ms)
uint32_t mv; //采样值 mv，
uint32_t r;//采样电阻，毫欧
uint32_t ua;//当前电流微安
uint32_t uams=0;//微安毫秒累加
uint32_t uas=0;//微安秒累加
uint32_t uam=0;//微安分钟 累加
uint32_t m_uam=0;   //分钟平均电流
uint32_t m_uams=0; //分钟累计电流


void geti(){
  if(r333==LOW) i0=analogRead(I333); //如果330欧开启，则使用I333的ADC腿
  else if(r33==LOW or r3==LOW) i0=analogRead(I33); //如果33欧或者3.3欧开启，则使用I33采样
  else  i0=analogRead(I03); //使用I03采样
  r3=digitalRead(R3); 
  r33=digitalRead(R33);
  r333=digitalRead(R333);
  r=330; //计算采样电阻值
  if(r3==LOW) r+=3300;
  if(r33==LOW) r+=33000;
  if(r333==LOW) r+=330000;
  mv=(uint32_t)i0*1100/1024; //mv
  ua=mv*1000*1000/r; //计算电流 单位ua
}
void seta(){ //每毫秒一次时间中断服务，采样电流和电压， 根据电流切换档位，
  ms++;  //毫秒计数
  uams+=ua; //微安*毫秒 累加
  if(ms>=1000) {   //整秒？
    ms=0;
    uas+=(uams/1000);  //微安*秒 累加
    uams=0;
    s++;
    if(s>=60) {
      s=0;
      m++;
      m_uam=uas/60;  //微安每分钟平均值
      uas=0;
      uam+=m_uam; //微安每分钟 累加值
      m_uams+=m_uam;
      m_save=true; //存数标志， 让主循环去保存数据。
      if(m>=60) {
        m=0;
        h++;
      }
    }
  }
  if(i_error>1) { //处理大电流报警
    i_error--;
    digitalWrite(VOUT,HIGH); //输出off
    return;
  }
  if(i_error==1) {

    digitalWrite(VOUT,LOW); //输出on
    i_error=0;
  }
  geti();
  if(ua>2000000) {
    i_error=10000; //电流超过2A，关闭10秒
    return;
  }
  if(ua<1000) { //使用366.63欧姆采样
    if(r3!=LOW || r33!=LOW || r333 !=LOW){ 
      digitalWrite(R3,LOW);
      digitalWrite(R33,LOW);
      digitalWrite(R333,LOW);
      geti();
    }
  }
  else if(ua<10000) { //电流1-10ma 用36.63欧姆采样
    if(r3!=LOW || r33!=LOW || r333 !=HIGH){ 
      digitalWrite(R3,LOW);
      digitalWrite(R33,LOW);
      digitalWrite(R333,HIGH);
      geti();
    }
  }
  else if(ua<100000) { //电流10-100ma，用3.63欧姆
    if(r3!=LOW || r33!=HIGH || r333 !=HIGH){ 
      digitalWrite(R3,LOW);
      digitalWrite(R33,HIGH);
      digitalWrite(R333,HIGH);
      geti();
    }
  }
  else { //大于100ma，用0.33欧姆
    if(r3!=HIGH || r33!=HIGH || r333 !=HIGH){ 
      digitalWrite(R3,HIGH);
      digitalWrite(R33,HIGH);
      digitalWrite(R333,HIGH);
      geti();
    }
  }
  v=getv();//电池电压
}
uint32_t getv() {//mv
  return (uint32_t) analogRead(VBAT)*11*1100/1024-mv;
}
char filename[15];
void init_filename() {
uint16_t eedat;
  eedat=EEPROM.read(0xff)+0x100;  //为了防止频繁取写同一个地址， 没1000次写入， 就换一个存储器
  file_no=EEPROM.read(eedat)<<8 | EEPROM.read(eedat+0x100); //使用0x100-0x2ff;  
  file_no++;  //每次启动都把文件序号加一
  if(file_no>=100000) file_no=0; //最大10万；
  if(file_no/1000 != eedat) { //每100次换个地址
    eedat=file_no/1000;
    EEPROM.write(0xff,eedat);//
  }
  snprintf(filename,13,"dat%05d.txt",file_no);
}
void setup() { 
  analogReference(INTERNAL);//atmega328 -> 基准电压1.1v
  analogRead(A0);   
  Serial.begin(115200);
  Serial.print("Battery logger ,V1.0"); 
  lcd.begin(16, 2);
  pinMode(VOUT,OUTPUT); 
  digitalWrite(VOUT,LOW);   //打开输出
  pinMode(led, OUTPUT);
  analogWrite(LAMP,50); 
  MsTimer2::set(1, seta); //每 1ms 时间中断一次， 调用seta();
  pinMode(R3,OUTPUT);
  digitalWrite(R3,HIGH);  
  pinMode(R33,OUTPUT);
  digitalWrite(R33,HIGH);
  pinMode(R333,OUTPUT);
  digitalWrite(R333,HIGH);
  MsTimer2::start(); //1ms每次的时间中断开始。

  if(EEPROM.read(0x301)!='L' && EEPROM.read(0x202) !='S') {//初始化eeprom
    for(uint16_t i=0;i<0x200;i++) EEPROM.write(i,0);
    EEPROM.write(0x301,'L');
    EEPROM.write(0x202,'S');    
  }
  have_sd=sd.begin(chipSelect, SPI_HALF_SPEED);
  if(have_sd) {
    init_filename();//根据file_no 生成文件名，放在filename
    if(myFile.open(filename, O_RDWR | O_CREAT | O_AT_END)) {
      myFile.println("hour,minute,I(ua),total(ua*minute)");
      myFile.close();
    }
  }
}
void lcd_f2(uint16_t dat){//除以1000显示2位小数
  uint16_t xs;
  dat=dat/10;
  xs=dat%100;
  lcd.print(dat/100);
  lcd.print('.');
  if(xs<10) lcd.print('0');
  lcd.print(xs);
}

void msave(){//每分钟写一次cdcard

uint16_t eedat;
  if(!myFile.open(filename, O_RDWR | O_CREAT | O_AT_END)) return;
  myFile.print(h);
  myFile.print(",");
  myFile.print(m);
  myFile.print(",");
  myFile.print(m_uam);
  myFile.print(",");
  myFile.println(m_uams);
  myFile.close();
  if(m_uams > 100 && file_no!=0) {  //只有真正有非零数据写入sdcard，才保存file_no到eeprom, 不修改file_no，则下次启动，会使用相同的文件名覆盖。
    eedat=EEPROM.read(0xff)+0x100;
    EEPROM.write(eedat,file_no>>8);
    EEPROM.write(eedat+0x100,file_no&0xff);
    file_no=0;
  }
}
void loop() {
  if(ms%500!=0) {
//不到0.5S， cpu休眠，等待time中断唤醒。
    set_sleep_mode (SLEEP_MODE_PWR_SAVE);
    sleep_enable();
    sleep_cpu (); 
}
  lcd.setCursor(0,0);
  if(i_error>0) { //大电流保护，
    analogWrite(LAMP,i_error/5%200+50); //背光闪烁 /5是慢一点， %200是 0-200调光， +50是背光调整到50-250之间变化， 一秒一个循环。
    if(i_error%1000==0) { //1秒钟执行一次，
      lcd.print("out>2A poweroff! ");
      lcd.setCursor(0,1);
      lcd_f2(getv());
      lcd.print("V   ");
      lcd.print(i_error/1000);
      lcd.print("    ");
    }
    return;
  }

  lcd_f2(getv()); //显示电池电压，2位小数
  lcd.print("V ");
  if(ua<1000){
    lcd.print(ua); //电流
    lcd.write(0xe4); //微
  }
  else{
    if(ua<10000) 
      lcd_f2(ua);
    else lcd.print(ua/1000);
    lcd.print('m');
  }
  lcd.print("A                ");
  lcd.setCursor(14,0);
  if(have_sd) lcd.print("S");//在存入sdcard时，让屏幕的S闪动一下
  else lcd.print(" ");
  if(r333==LOW) lcd.print('D'); //第4档位测流
  else if(r33==LOW) lcd.print('C');//第3档位
  else if(r3== LOW) lcd.print('B'); //第2档位
  else lcd.print('A'); //第1档位
  lcd.setCursor(0,1);   //lcd第二行
  if(m_uams==0){
    if(have_sd) lcd.print(file_no); //开始无电流时显示当次sd文件序号
    else
    lcd.print("0 mAH      ");  //无sd卡时显示0mah
  }
  else if(m_uams<1000) { //1-1000微安*分，单位显示uAM
    lcd.print(m_uams);
    lcd.write(0xe4);
    lcd.print("AM  ");
  }
  else{
    if(m_uams<60000) {  //1000微安时以下,单位显示uAH
      lcd.print(m_uams/60); //微安*分钟->微安*时
      lcd.write(0xe4); 
    }
    else{ //超过1000微安时，单位显示mAH
      lcd.print(m_uams/60000); //微安*分钟->毫安*时
      lcd.print("m"); 
    }
    lcd.print("AH   "); //清后面的残留字符
  }
  lcd.setCursor(8,1);//显示开机时间
  if(h<10) lcd.print('0');
  lcd.print(h);
  lcd.print(':');
  if(m<10) lcd.print('0');
  lcd.print(m);
  lcd.print(':');
  if(s<10) lcd.print('0');
  lcd.print(s);
  digitalWrite(led, !digitalRead(led));   // led闪烁
  if(m_save==true) { //保存分钟数据
    if(have_sd) msave();
    m_save=false;
    lcd.setCursor(14,0);
    lcd.print(" ");
  }
  delay(500);               // 半秒
}





