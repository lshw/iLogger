#define  NEWPCB 1   //如果是输出接口是2个腿的，就是老版本的，要把本行注释掉,
//型号选择UNO或者  LiLyPad Arduino 168,或者LiLyPad Arduino 328, 其中168 ram/rom都不够，所以无SD卡功能
//需要安装 MsTimer2 lib
#include <EEPROM.h>
#include <LiquidCrystal.h>
#include <MsTimer2.h>

#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/sleep.h>
#if defined(__AVR_ATmega328P__) //  168内存不够，不能带SD卡功能
#include <SPI.h>
#include <SD.h>
File myFile;
#define chipSelect  10
#endif
const char  *logo  = "github.com/lshw ";
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

uint8_t buff[120], bf = 0, tf = 0;
uint32_t recTime = 0;
boolean have_sd = false;
uint16_t file_no;
uint32_t last0 = 0; //最后非零时间 用于休眠
LiquidCrystal lcd(8, LCDEN, 6, 5, 4, 3); //rs,en,d4,d5,d6,d7
__volatile__ uint16_t i0;
uint8_t m = 0, s = 0, adc = I333;
uint16_t ms = 0;
uint16_t h = 0;
boolean m_save = false; //主循环需要保存当前分钟电流
uint16_t  v, i_error_ma, i_error = 0; //v->电池电压mv，电流大于2A，报警倒计数（ms)
uint32_t mv; //采样值 mv，
uint32_t r;//采样电阻，毫欧
uint32_t ua = 0; //当前电流微安
uint32_t is = 0; //前一秒的平均电流
uint32_t uams = 0; //微安毫秒累加
uint32_t uas = 0; //微安秒累加
uint32_t uam = 0; //分钟平均电流
uint32_t m_uams = 0; //分钟累计电流
void geti() {
  for (uint8_t i = 0; i < 3; i++) { //循环切换档位
    i0 = analogRead(adc);
    if (i0 > 300) i0 = analogRead(adc);
    if (i0 > 200 && r > 330) { //调低采样电阻
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
        digitalWrite(R333, HIGH);
        digitalWrite(R33, HIGH);
        digitalWrite(R3, HIGH);
        r = 330;
        adc = I03;
      }
      continue;//循环切换档位
    } else if (i0 < 15 & r <  330000) { //调高
      if (r <= 330) {
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
    is = uams / 1000; //前一秒的平均电流
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
    geti();
    geti();
    if (ua > 2000000) {
      i_error_ma = ua / 1000;
      i_error = 10000; //电流超过2A，关闭10秒
    }
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
  eedat = EEPROM.read(0xff) + 0x100; //为了防止频繁取写同一个地址， 每1000次写入， 就换一个存储器
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
  uint8_t ch;
  for (i = 0; i < 16; i++) {
    ch = EEPROM.read(i + 0x11);
    if (ch<' ' or ch >= 0x80) {
      EEPROM.write(0x304, 0);
      break;
    }
  }
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

boolean dogup = false;
#define REBOOT_   asm volatile ("  jmp 0")
uint8_t dogcount = 0;
ISR(WDT_vect) {
  dogup = true;
  if (dogcount > 10) REBOOT_;
}

void setup_watchdog(int ii) {
  byte bb;
  if (ii > 9 ) ii = 9;
  bb = ii & 7;
  if (ii > 7) bb |= (1 << 5);
  bb |= (1 << WDCE);
  MCUSR &= ~(1 << WDRF);
  // start timed sequence
  WDTCSR |= (1 << WDCE) | (1 << WDE);
  // set new watchdog timeout value
  WDTCSR = bb;
  WDTCSR |= _BV(WDIE);
}
uint16_t bat_v0, bat_r; //电池的空载电压，电池内阻
void setup() {
  uint16_t i;
  uint8_t oumchar[8] = {  /*欧姆*/
    0b11011,
    0b10101,
    0b10101,
    0b00000,
    0b11111,
    0b10001,
    0b01010,
    0b11011
  };
  analogReference(INTERNAL);//atmega328 -> 基准电压1.1v
  pinMode(VOUT, OUTPUT);
  digitalWrite(VOUT, HIGH);  //关闭输出
  i = (uint32_t) analogRead(VBAT) * 11 * 1100 / 1024; //电池电压
  delay(10);
  bat_v0 = (uint32_t) analogRead(VBAT) * 11 * 1100 / 1024; //电池电压
  MsTimer2::set(1, seta); //每 1ms 时间中断一次， 调用seta();
  pinMode(R3, OUTPUT);
  pinMode(R33, OUTPUT);
  pinMode(R333, OUTPUT);
  digitalWrite(R3, HIGH);
  digitalWrite(R33, HIGH);
  digitalWrite(R333, HIGH);
  r = 330;
  MsTimer2::start(); //1ms每次的时间中断开始。
  delay(2);
  digitalWrite(VOUT, LOW);  //打开输出
  pinMode(LAMP, OUTPUT);
  analogWrite(LAMP, 40);
  eeprom_init();
  lcd.begin(16, 2);
  lcd.createChar(1, oumchar);   //om
  Serial.begin(115200);
  Serial.println(F("iLogger V1.9"));
  lcd.setCursor(0, 0);
  lcd.print(F("iLogger V1.9"));
  lcd.setCursor(0, 1);
  for (i = 0; i < 16; i++)
    lcd.write(EEPROM.read(i + 0x11));
#if defined(__AVR_ATmega328P__)
  have_sd = SD.begin(chipSelect);
  if (have_sd) {
    init_filename();//根据file_no 生成文件名，放在filename
    SD.remove(filename);
    myFile = SD.open(filename, FILE_WRITE);
    if (myFile) {
      myFile.println(F("hh:mm:ss,hour,minute,V(mv),I(ua),total(ua*minute)"));
      myFile.close();
    }
  }
#endif
  setup_watchdog(WDTO_4S); //4s
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
#if defined(__AVR_ATmega328P__)
void msave() { //每分钟写一次cdcard
  myFile = SD.open(filename, FILE_WRITE);
  if (!myFile) return;
  myFile.print("\"");
  if (h < 10) myFile.print("0");
  myFile.print(h);
  myFile.print(":");
  if (m < 10) myFile.print("0");
  myFile.print(s);  myFile.print(":");
  if (m < 10) myFile.print("0");
  myFile.print(s);
  myFile.print("\",");
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
    file_no_inc();
  }
}
#endif
void file_no_inc() {
  uint16_t eedat;
  if (file_no == 0) return;
  eedat = EEPROM.read(0xff) + 0x100; //index [0xff]=
  EEPROM.write(eedat, file_no >> 8);
  EEPROM.write(eedat + 0x100, file_no & 0xff);
  file_no = 0;
}
#if defined(__AVR_ATmega328P__)

void com2sd() {
  if (bf == tf) return;

  analogWrite(LAMP, 0);
  if (have_sd) {
    myFile = SD.open(filename, FILE_WRITE);
    if (!myFile) return;
    myFile.print("\"");
    if (h < 10) myFile.print("0");
    myFile.print(h);
    myFile.print(":");
    if (m < 10) myFile.print("0");
    myFile.print(m);
    myFile.print(":");
    if (s < 10) myFile.print("0");
    myFile.print(s);
    myFile.print("\",\"");
  }
  while (1) {
    if (bf == tf) break;
    if (have_sd)  myFile.write(buffget());
    else buffget();
  }
  if (have_sd) {
    myFile.println("\"");
    myFile.close();
  }
  analogWrite(LAMP, 40);
  if (file_no != 0) file_no_inc();
};
#endif
void power_down() {
  uint16_t i;
  for (i = 2; i < 23; i++) {
    pinMode(i, INPUT);
    digitalWrite(i, LOW);
  }
  digitalWrite(LAMP, LOW);
  digitalWrite(VOUT, HIGH); //输出off
  lcd.noDisplay();
  set_sleep_mode (SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  if (v < 3100) wdt_disable(); //电压过低， 不再唤醒

  ADCSRA &=  ~(1 << ADEN);  // 0
  Serial.end();
  sleep_cpu ();
  ADCSRA |=  (1 << ADEN);
  analogRead(adc);
  s = s + 4; //4秒唤醒;
  if (s >= 60) {
    m++;
    s = s - 60;
  }
  if (m >= 60) {
    h++;
    m = 0;
    Serial.begin(115200);
    if (h < 10) Serial.print('0');
    Serial.print(h);
    Serial.print(':');
    if (m < 10) Serial.print('0');
    Serial.print(m);
    Serial.print(' ');
    Serial.print(m_uams);
    Serial.flush();
  }
  lcd.begin(16, 2);
}
boolean poweroff = false;
void loop() {
  uint16_t i;
  if (poweroff == false) {
    if (ms % 500 != 0) {
      //不到0.1S， cpu休眠，等待time中断唤醒。
      set_sleep_mode (SLEEP_MODE_IDLE); //虽然IDLE模式省电不多， 但是不影响PWM输出的背光控制。
      sleep_enable();
      sleep_cpu ();
      analogRead(adc);
      return;
    }
    analogWrite(LAMP, 40);
  }
  lcd.setCursor(0, 0);
  if (i_error > 0) { //大电流保护，
    analogWrite(LAMP, i_error / 5 % 200 + 50); //背光闪烁 /5是慢一点， %200是 0-200调光， +50是背光调整到50-250之间变化， 一秒一个循环。
    lcd.print(F("out>2A poweroff! "));
    digitalWrite(VOUT, HIGH);
    lcd.setCursor(0, 1);
    lcd.print(i_error / 1000);
    lcd.write(' ');
    lcd.print(i_error_ma);
    lcd.print(F("ma"));
    return;
  }

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
  lcd.print(F("A                "));
  lcd.setCursor(14, 0);
  if (have_sd) lcd.print("S"); //在存入sdcard时，让屏幕的S闪动一下
  else lcd.print(" ");
  if (r == 330) lcd.print('D'); //第4档位测流
  else if ( r == 330 + 3300) lcd.print('C'); //第3档位
  else if (r == 330 + 3300 + 33000) lcd.print('B'); //第2档位
  else lcd.print('A'); //第1档位
  lcd.setCursor(0, 1);  //lcd第二行
  if (m_uams == 0) {
    if (ua > 10000 && millis() > 1000 && millis() < 50000) { //在开机1秒后和10秒前， 显示电池内阻
      bat_r = (uint32_t) (bat_v0 - v) * 1000 / (ua / 1000);
      Serial.print(F("bat_v0:")); Serial.println(bat_v0);
      Serial.print(F("v:")); Serial.println(v);
      Serial.print(F("ua:")); Serial.println(ua);
      Serial.print(F("bat_r:")); Serial.println(bat_r);
      lcd.print(F("R="));  //
      lcd.print(bat_r);  //显示电池内阻
      lcd.write(0x01);
      lcd.print(F("         ")); //毫欧姆
    } else {
      if (have_sd) {
        for (i = 3; i < 8; i++)
          lcd.print(filename[i]);//开始无电流时显示当次sd文件序号
        lcd.print(F("         "));
      } else
        lcd.print(F("0 mAH      "));  //无sd卡时显示0mah
    }
  }
  else if (m_uams < 1000) { //1-1000微安*分，单位显示uAM
    lcd.print(m_uams);
    lcd.write(0xe4);
    lcd.print(F("AM  "));
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
    lcd.print(F("AH   ")); //清后面的残留字符
  }
  if (h >= 1000) { //9999-1000
    lcd.setCursor(6, 1);
  } else if (h >= 100) { //999-100
    lcd.setCursor(7, 1);
  } else { //99-00
    lcd.setCursor(8, 1); //显示开机时间
    if (h < 10) lcd.print('0');
  }
  lcd.print(h);
  lcd.print(':');
  if (m < 10) lcd.print('0');
  lcd.print(m);
  lcd.print(':');
  if (s < 10) lcd.print('0');
  lcd.print(s);
  if (m_save == true) { //保存分钟数据
#if defined(__AVR_ATmega328P__)
    if (have_sd) msave();
#endif
    m_save = false;
    lcd.setCursor(14, 0);
    lcd.print(" ");
  }

  dogcount = 0;//喂狗

  if (last0 + 600000 < millis() || v < 3100) {
    if (v < 3100) delay(100);
    if (last0 + 600000 < millis() || v < 3100) {
      MsTimer2::stop(); //1ms每次的时间中断关闭。
      poweroff = true;
      wdt_reset();
      power_down();
    }
  }
#if defined(__AVR_ATmega328P__)
  if (millis() > recTime + 200 & bf != tf) {
    com2sd();
  }
#endif
}
void buffput(uint8_t dat)
{
  uint8_t offset = tf + 1;
  if (offset >= 120) offset = 0;
  if (offset == bf) return;
  buff[tf] = dat;
  tf = offset;
}
int16_t buffget() {
  uint8_t offset;
  if (bf == tf) return -1;
  offset = bf;
  bf++;
  if (bf >= 120) bf = 0;
  return (buff[offset]);
}
void serialEvent() {
  while (Serial.available()) {
    recTime = millis();

    buffput((char)Serial.read());
  }
}
