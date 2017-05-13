#define  NEWPCB 1   //如果是输出接口是2个腿的，就是老版本的，要把本行注释掉，
#include <EEPROM.h>
#include <LiquidCrystal.h>
#include <MsTimer2.h>
#include <avr/sleep.h>
#include <SdFat.h>
SdFat sd;
SdFile myFile;
#define chipSelect  10
const char * logo = "github.com/lshw ";
#define VBAT A0 //电源电压
#define VOUT A1  //LOW-打开输出
#ifdef NEWPCB
#define R3 A3   //LOW 采样电阻加3.3欧
#define R33 A4  //LOW 采样电阻加33欧
#else
#define R3 A4   //LOW 采样电阻加3.3欧
#define R33 A3  //LOW 采样电阻加33欧
#endif
#define R333 2  //LOW 采样电阻加330欧
#define I333 A7  //330欧的AD
#define I33 A5  //33欧的AD
#define I03 A2 //0.33欧的AD
#define LAMP 9  //LCD背光
#define LCDEN 7


boolean have_sd = false;
uint16_t file_no;
uint32_t last0 = 0; //最后非零时间 用于休眠
LiquidCrystal lcd(8, LCDEN, 6, 5, 4, 3); //rs,en,d4,d5,d6,d7
__volatile__ uint16_t i0;
uint8_t h = 0, m = 0, s = 0, adc = I333;
uint16_t ms = 0;
boolean m_save = false; //主循环需要保存当前分钟电流
uint16_t  v, i_error = 0; //v->电池电压mv，电流大于2A，报警倒计数（ms)
uint32_t mv; //采样值 mv，
uint32_t r;//采样电阻，毫欧
uint32_t ua = 0; //当前电流微安
uint32_t is=0; //前一秒的平均电流
uint32_t uams = 0; //微安毫秒累加
uint32_t uas = 0; //微安秒累加
uint32_t uam = 0; //分钟平均电流
uint32_t m_uams = 0; //分钟累计电流
void geti() {
  for (uint8_t i = 0; i < 3; i++) { //循环切换档位
    i0 = analogRead(adc);
    if (i0 > 300) i0 = analogRead(adc);
    if (i0 > 200 & r > 330) { //调低采样电阻
      if (r >= 330000) {
        digitalWrite(R333, HIGH);
        digitalWrite(R33, LOW);
        digitalWrite(R3, LOW);
        r = 330 + 3300 + 33000; //lll->llh
        adc = I33;
      } else if (r >= 33000) {
        digitalWrite(R333, HIGH);
        digitalWrite(R33, HIGH);
        digitalWrite(R3, LOW);
        r = 330 + 3300;
        adc = I33;
      } else if (r >= 3300) {
        Serial.println(i0);
        digitalWrite(R333, HIGH);
        digitalWrite(R33, HIGH);
        digitalWrite(R3, HIGH);
        r = 330;
        adc = I03;
      }
      continue;//循环切换档位
    } else if (i0 < 15 & r <  330000) { //调高
      if (r <= 330) {
        Serial.println(i0);
        digitalWrite(R3, LOW);
        digitalWrite(R33, HIGH);
        digitalWrite(R333, HIGH);
        r = 330 + 3300;
        adc = I33;
      } else if (r <= 330 + 3300) {
        digitalWrite(R3, LOW);
        digitalWrite(R33, LOW);
        digitalWrite(R333, HIGH);
        r = 330 + 3300 + 33000;
        adc = I33;
      } else if (r <= 330 + 3300 + 33000) {
        digitalWrite(R3, LOW);
        digitalWrite(R33, LOW);
        digitalWrite(R333, LOW);
        r = 330 + 3300 + 33000 + 330000;
        adc = I333;
      }
      continue;//循环切换档位
    }
    break;//终止切换档位循环
  }
  mv = (uint32_t)i0 * 1100 / 1024; //mv
  ua = mv * 1000 * 1000 / r; //计算电流 单位ua
}
void seta() { //每毫秒一次时间中断服务，采样电流和电压， 根据电流切换档位，
  ms++;  //毫秒计数
  geti();
  uams += ua; //微安*毫秒 累加
  if (ms >= 1000) { //整秒？
    ms = 0;
    is=uams/1000; //前一秒的平均电流
    uas += is; //微安*秒 累加
    uams = 0;
    s++;
    if (s >= 60) {
      s = 0;
      m++;
      uam = uas / 60; //微安每分钟平均值
      uas = 0;
      m_uams += uam; //微安每分钟 累加值
      m_save = true; //存数标志， 让主循环去保存数据。
      if (m >= 60) {
        m = 0;
        h++;
      }
    }
  }
  if (i_error > 1) { //处理大电流报警
    i_error--;
    digitalWrite(R333, LOW); //输出off
    return;
  }
  if (i_error == 1) {
    geti();
    i_error = 0;
  }
  if (ua > 2000000) {
    delay(1);
    geti();
    if (ua > 2000000)
      i_error = 10000; //电流超过2A，关闭10秒
    return;
  }
  if (ua != 0) {
    last0 = millis();
  }
  v = (uint32_t) analogRead(VBAT) * 11 * 1100 / 1024 - mv; //电池电压
}
char filename[15] = "DAT00001.CSV\0";
void init_filename() {
  uint16_t eedat;
  eedat = EEPROM.read(0xff) + 0x100; //为了防止频繁取写同一个地址， 没1000次写入， 就换一个存储器
  file_no = EEPROM.read(eedat) << 8 | EEPROM.read(eedat + 0x100); //使用0x100-0x2ff;
  file_no++;  //每次启动都把文件序号加一
  if (file_no >= 100000) file_no = 0; //最大10万；
  if (file_no / 1000 != eedat) { //每1000次换个地址
    eedat = file_no / 1000;
    EEPROM.write(0xff, eedat); //
  }
  snprintf(filename, 13, "DAT%05d.TXT", file_no);
}
void eeprom_init() {
  uint16_t i;
  if (EEPROM.read(0x304) != 'S' || EEPROM.read(0x202) != 'W') { //初始化eeprom
    for (i = 0; i < 0x400; i++) EEPROM.write(i, 0);
    EEPROM.write(0x304, 'S');
    EEPROM.write(0x202, 'W');
    for (i = 0; i < 16; i++)
      if (strlen(logo) > i)
        EEPROM.write(i + 0x11, logo[i]);
      else
        EEPROM.write(i + 0x11, ' ');
  }
}
void setup() {
  const  char * hello = "iLogger V1.4";
  uint16_t i;
  pinMode(LAMP, OUTPUT);
  analogWrite(LAMP, 40);
  eeprom_init();
  analogReference(INTERNAL);//atmega328 -> 基准电压1.1v
  lcd.begin(16, 2);
  Serial.begin(115200);
  Serial.println(hello);
  lcd.print(hello);
  lcd.setCursor(0, 1);
  for (i = 0; i < 16; i++)
    lcd.write(EEPROM.read(i + 0x11));
  pinMode(VOUT, OUTPUT);
  digitalWrite(VOUT, LOW);  //打开输出
  MsTimer2::set(1, seta); //每 1ms 时间中断一次， 调用seta();
  pinMode(R3, OUTPUT);
  pinMode(R33, OUTPUT);
  pinMode(R333, OUTPUT);
  digitalWrite(R3, HIGH);
  digitalWrite(R33, HIGH);
  digitalWrite(R333, HIGH);
  r = 330;
  MsTimer2::start(); //1ms每次的时间中断开始。
  have_sd = sd.begin(chipSelect, SPI_HALF_SPEED);
  if (have_sd) {
    init_filename();//根据file_no 生成文件名，放在filename
    sd.remove(filename);
    if (myFile.open(filename, O_RDWR | O_CREAT | O_AT_END)) {
      myFile.println("hh:mm,hour,minute,V(mv),I(ua),total(ua*minute)");
      myFile.close();
    }
  }
}
void lcd_f2(uint16_t dat) { //除以1000显示2位小数
  uint16_t xs;
  dat = dat / 10;
  xs = dat % 100;
  lcd.print(dat / 100);
  lcd.print('.');
  if (xs < 10) lcd.print('0');
  lcd.print(xs);
}

void msave() { //每分钟写一次cdcard
  uint16_t eedat;
  if (!myFile.open(filename, O_RDWR | O_CREAT | O_AT_END)) return;
 
  myFile.print("\"");
  if(h<10) myFile.print("0");
  myFile.print(h);
  myFile.print(":");
  if(m<10) myFile.print("0");
  myFile.print(m);
  myFile.print("\"");
  myFile.print(",");
  myFile.print(h);
  myFile.print(",");
  myFile.print(m);
  myFile.print(",");
  myFile.print(v);
  myFile.print(",");
  myFile.print(uam);
  myFile.print(",");
  myFile.println(m_uams);
  myFile.close();
  if (m_uams > 100 && file_no != 0) { //只有真正有非零数据写入sdcard，才保存file_no到eeprom, 不修改file_no，则下次启动，会使用相同的文件名覆盖。
    eedat = EEPROM.read(0xff) + 0x100; //index [0xff]=
    EEPROM.write(eedat, file_no >> 8);
    EEPROM.write(eedat + 0x100, file_no & 0xff);
    file_no = 0;
  }
}
void power_down() {
  uint16_t i;
  for (i = 0; i < 23; i++) {
    pinMode(i, INPUT);
    digitalWrite(i, LOW);
  }
  digitalWrite(LAMP, LOW);
  digitalWrite(VOUT, HIGH); //输出off
  cli();
  CLKPR = B10000000;
  CLKPR = B00000011;   //调频率16Mhz到2Mhz
  sei();
  lcd.noDisplay();
  set_sleep_mode (SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_cpu ();
}
void loop() {
  uint16_t i;
  if (ms % 500 != 0) {
    //不到0.1S， cpu休眠，等待time中断唤醒。
    set_sleep_mode (SLEEP_MODE_IDLE); //虽然IDLE模式省电不多， 但是不影响PWM输出的背光控制。
    sleep_enable();
    sleep_cpu ();
    analogRead(adc);
    return;
  }

  lcd.setCursor(0, 0);
  if (i_error > 0) { //大电流保护，
    analogWrite(LAMP, i_error / 5 % 200 + 50); //背光闪烁 /5是慢一点， %200是 0-200调光， +50是背光调整到50-250之间变化， 一秒一个循环。
    lcd.print("out>2A poweroff! ");
    lcd.setCursor(0, 1);
    lcd.print(i_error / 1000);
    lcd.print("    ");
    return;
  }

  analogWrite(LAMP, 40);
  if (millis() < 2000) return;
  lcd_f2(v); //显示电池电压，2位小数
  lcd.print("V ");
  if (is < 1000) {
    lcd.print(is); //电流
    lcd.write(0xe4); //微
  }
  else {
    if (is < 10000)
      lcd_f2(is);
    else lcd.print(is / 1000);
    lcd.print('m');
  }
  lcd.print("A                ");
  lcd.setCursor(14, 0);
  if (have_sd) lcd.print("S"); //在存入sdcard时，让屏幕的S闪动一下
  else lcd.print(" ");
  if (r == 330) lcd.print('D'); //第4档位测流
  else if ( r == 330 + 3300) lcd.print('C'); //第3档位
  else if (r == 330 + 3300 + 33000) lcd.print('B'); //第2档位
  else lcd.print('A'); //第1档位
  lcd.setCursor(0, 1);  //lcd第二行
  if (m_uams == 0) {
    if (have_sd) {
      for (i = 3; i < 8; i++)
        lcd.print(filename[i]);//开始无电流时显示当次sd文件序号
      lcd.print("         ");
    } else
      lcd.print("0 mAH      ");  //无sd卡时显示0mah
  }
  else if (m_uams < 1000) { //1-1000微安*分，单位显示uAM
    lcd.print(m_uams);
    lcd.write(0xe4);
    lcd.print("AM  ");
  }
  else {
    if (m_uams < 60000) { //1000微安时以下,单位显示uAH
      lcd.print(m_uams / 60); //微安*分钟->微安*时
      lcd.write(0xe4);
    }
    else { //超过1000微安时，单位显示mAH
      lcd.print(m_uams / 60000); //微安*分钟->毫安*时
      lcd.print("m");
    }
    lcd.print("AH   "); //清后面的残留字符
  }
  lcd.setCursor(8, 1); //显示开机时间
  if (h < 10) lcd.print('0');
  lcd.print(h);
  lcd.print(':');
  if (m < 10) lcd.print('0');
  lcd.print(m);
  lcd.print(':');
  if (s < 10) lcd.print('0');
  lcd.print(s);
  if (m_save == true) { //保存分钟数据
    if (have_sd) msave();
    m_save = false;
    lcd.setCursor(14, 0);
    lcd.print(" ");
  }
  if (last0 + 600000 < millis() || v < 3200) {
    delay(100);
    if (last0 + 600000 < millis() || v < 3200)
      power_down();
  }
}
