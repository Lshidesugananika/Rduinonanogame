#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <avr/pgmspace.h>

// --- システム共通設定 ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define BTN_LEFT  2
#define BTN_RIGHT 3
#define BTN_ACT   4 
#define SPK_PIN   11

enum AppMode { MODE_MENU, MODE_JUMP, MODE_RACE };
AppMode currentMode = MODE_MENU;

// --- 音階定義 ---
#define NOTE_G3 196
#define NOTE_A3 220
#define NOTE_B3 247
#define NOTE_C4 262
#define NOTE_D4 294
#define NOTE_E4 330
#define NOTE_F4 349
#define NOTE_G4 392
#define NOTE_A4 440
#define NOTE_B4 494
#define NOTE_C5 523
#define NOTE_E3 165 // レースBGM用

// --- 共通リソース (PROGMEM) ---
// [JUMP] 炎の敵
static const unsigned char PROGMEM flame_bitmap[] = {
  B00100000, B01110000, B01110000, B11111000, B11111000, 
  B01110000, B01110000, B00100000, B00100000, B00100000
};
// [RACE] 車
const unsigned char PROGMEM race_car_sprite[] = {
  B00000011, B11000000, B00000111, B11100000, B00011111, B11111000,
  B01111111, B11111110, B11011100, B00111011, B11111111, B11111111,
  B11101110, B01110111, B11101110, B01110111
};

// --- BGMデータ ---
// [JUMP GAME BGM] 明るいCメジャーの曲
const int bgm_melody_jump[] PROGMEM = {
  NOTE_C4, NOTE_E4, NOTE_G4, NOTE_C5, NOTE_G4, NOTE_E4, NOTE_C4, 0,
  NOTE_F4, NOTE_A4, NOTE_C5, NOTE_A4, NOTE_F4, NOTE_A4, NOTE_F4, 0,
  NOTE_G4, NOTE_B4, NOTE_D4, NOTE_B4, NOTE_G4, NOTE_F4, NOTE_D4, NOTE_B3,
  NOTE_C4, NOTE_E4, NOTE_G4, NOTE_E4, NOTE_C4, 0, 0, 0
};

// [RACE GAME BGM] 疾走感のあるマイナー調
const int bgm_melody_race[] PROGMEM = {
  NOTE_E3, NOTE_E3, NOTE_G3, NOTE_E3, NOTE_A3, NOTE_E3, NOTE_G3, NOTE_B3,
  NOTE_E3, NOTE_E3, NOTE_G3, NOTE_E3, NOTE_A3, NOTE_G3, NOTE_E3, NOTE_D4
};

// --- JUMP GAME 変数 ---
struct JumpEnemy { float x, y, spd, start, end; int dir; bool alive; };
struct Platform { float x, w, y; };
JumpEnemy j_enemies[3];
Platform j_platforms[6];
float j_playerX, j_playerY, j_velY, j_worldOffset;
bool j_onGround;
int j_state; // 0:Play, 1:Over, 2:Clear
unsigned long j_startTime;
// ★2D用BGM変数
int j_bgmIndex = 0;
unsigned long j_lastNoteTime = 0;

// --- RACE GAME 変数 ---
struct RaceEnemy { float x, z; bool active; };
RaceEnemy r_enemies[3];
float r_playerX, r_speed, r_roadOffset;
int r_rank, r_bgmIndex;
unsigned long r_lastNoteTime;
bool r_gameOver;

// ==========================================
// セットアップ & メニュー
// ==========================================
void setup() {
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_ACT, INPUT_PULLUP);
  pinMode(SPK_PIN, OUTPUT);

  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) for(;;);
  display.clearDisplay();
  display.display();
}

void loop() {
  if (currentMode == MODE_MENU) {
    runMenu();
  } else if (currentMode == MODE_JUMP) {
    runJumpGame();
  } else if (currentMode == MODE_RACE) {
    runRaceGame();
  }
}

// --- メニュー画面処理 ---
int menuSelect = 0; 
void runMenu() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 5);
  display.println(F("GAME MENU"));

  display.setTextSize(1);
  
  if(menuSelect == 0) { display.fillRect(0, 30, 128, 14, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); } 
  else { display.setTextColor(SSD1306_WHITE); }
  display.setCursor(10, 33); display.println(F("1. 2D ACTION JUMP"));

  if(menuSelect == 1) { display.fillRect(0, 48, 128, 14, SSD1306_WHITE); display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); } 
  else { display.setTextColor(SSD1306_WHITE); }
  display.setCursor(10, 51); display.println(F("2. 3D RACE GP"));

  display.setTextColor(SSD1306_WHITE);
  display.drawRect(0,0,128,64,SSD1306_WHITE);
  display.display();

  if (digitalRead(BTN_LEFT) == LOW) { menuSelect = 0; delay(150); tone(SPK_PIN, 1000, 20); }
  if (digitalRead(BTN_RIGHT) == LOW) { menuSelect = 1; delay(150); tone(SPK_PIN, 1000, 20); }
  if (digitalRead(BTN_ACT) == LOW) {
    tone(SPK_PIN, 2000, 100); delay(200);
    if (menuSelect == 0) { j_setup(); currentMode = MODE_JUMP; }
    else { r_setup(); currentMode = MODE_RACE; }
  }
}

// ==========================================
// GAME 1: 2D JUMP ACTION
// ==========================================
void j_setup() {
  int py = 58;
  j_platforms[0]={-20,170,py}; j_platforms[1]={180,50,py-12}; j_platforms[2]={240,50,py-24};
  j_platforms[3]={320,100,py}; j_platforms[4]={450,100,py-15}; j_platforms[5]={600,200,py};
  
  j_playerX = 10; j_playerY = py-8; j_velY = 0; j_worldOffset = 0; j_onGround = true;
  j_state = 0; j_startTime = millis();
  j_enemies[0]={330,py-10,1.0,320,415,-1,true};
  j_enemies[1]={460,py-25,1.2,450,545,-1,true};
  j_enemies[2]={610,py-10,0.8,600,795,-1,true};
  
  // BGMリセット
  j_bgmIndex = 0;
  j_lastNoteTime = millis();
}

void runJumpGame() {
  if (j_state != 0 && digitalRead(BTN_ACT) == LOW) {
    j_setup(); delay(500); return;
  }

  if (j_state == 0) { 
    // ★ 2Dゲーム BGM処理
    if (millis() - j_lastNoteTime > 150) { // テンポ 150ms固定
      j_lastNoteTime = millis();
      int note = pgm_read_word_near(bgm_melody_jump + j_bgmIndex);
      if(note > 0) tone(SPK_PIN, note, 120); // 音鳴らす
      // 次の音へ
      j_bgmIndex++;
      if(j_bgmIndex >= 32) j_bgmIndex = 0; // ループ (曲長に合わせて調整)
    }

    // ロジック
    bool L = !digitalRead(BTN_LEFT), R = !digitalRead(BTN_RIGHT), J = !digitalRead(BTN_ACT);
    if (R) j_playerX += 2.5;
    if (L && (j_playerX - j_worldOffset) > 0) j_playerX -= 2.5;
    
    if (J && j_onGround) { j_velY = -5.5; j_onGround = false; tone(SPK_PIN,1200,80); }
    if (!j_onGround) j_velY += 0.4; 

    float nextY = j_playerY + j_velY;
    bool landed = false;
    if (j_velY >= 0) {
      for(int i=0;i<6;i++) {
        if(j_playerX+8 > j_platforms[i].x && j_playerX < j_platforms[i].x+j_platforms[i].w) {
          if(j_playerY+8 <= j_platforms[i].y && nextY+8 >= j_platforms[i].y) {
            j_playerY = j_platforms[i].y - 8; j_velY=0; landed=true; break;
          }
        }
      }
    }
    if(!landed) { j_playerY = nextY; j_onGround = false; } else j_onGround = true;

    if(j_playerY > 64) { j_state=1; noTone(SPK_PIN); tone(SPK_PIN,150,400); }

    float screenX = j_playerX - j_worldOffset;
    if(screenX > 64) j_worldOffset += (screenX - 64);
    if(j_worldOffset < 0) j_worldOffset = 0;

    for(int i=0;i<3;i++) {
      if(!j_enemies[i].alive) continue;
      j_enemies[i].x += j_enemies[i].spd * j_enemies[i].dir;
      if(j_enemies[i].x < j_enemies[i].start || j_enemies[i].x > j_enemies[i].end) j_enemies[i].dir *= -1;
      if(j_playerX < j_enemies[i].x+5 && j_playerX+8 > j_enemies[i].x &&
         j_playerY < j_enemies[i].y+10 && j_playerY+8 > j_enemies[i].y) {
           j_state=1; noTone(SPK_PIN); tone(SPK_PIN,150,400);
      }
    }

    if(j_playerX > 780 && j_onGround && j_playerY == j_platforms[5].y-8) {
      j_state=2; noTone(SPK_PIN);
      tone(SPK_PIN,1046,100); delay(120); tone(SPK_PIN,1318,100); delay(120); tone(SPK_PIN,2093,400);
    }
  }

  // 描画
  display.clearDisplay();
  for(int i=0;i<6;i++) display.fillRect(j_platforms[i].x - j_worldOffset, j_platforms[i].y, j_platforms[i].w, 64, SSD1306_WHITE);
  for(int i=0;i<3;i++) if(j_enemies[i].alive) display.drawBitmap(j_enemies[i].x - j_worldOffset, j_enemies[i].y, flame_bitmap, 5, 10, SSD1306_WHITE);
  int gx = 780 - j_worldOffset, gy = j_platforms[5].y - 30;
  display.drawFastVLine(gx, gy, 30, SSD1306_WHITE);
  display.fillTriangle(gx, gy, gx, gy+8, gx-8, gy+4, SSD1306_WHITE);
  if(j_state != 1) display.fillRect(j_playerX - j_worldOffset, j_playerY, 8, 8, SSD1306_WHITE);

  display.setCursor(0,0); 
  if(j_state==0) { display.print("TIME:"); display.print((millis()-j_startTime)/1000); }
  else if(j_state==1) display.print("GAME OVER! Push Btn");
  else display.print("CLEAR!! Push Btn");
  display.display();
}

// ==========================================
// GAME 2: 3D RACE GP
// ==========================================
void r_setup() {
  r_playerX = 0; r_speed = 0; r_rank = 50; r_gameOver = false; r_roadOffset = 0;
  r_bgmIndex = 0; r_lastNoteTime = millis();
  for(int i=0;i<3;i++) { r_enemies[i].active=false; r_enemies[i].z=0; }
  tone(SPK_PIN,440,100); delay(150); tone(SPK_PIN,440,100); delay(150); tone(SPK_PIN,880,400);
}

void drawRaceCar(int x, int y, float s) {
  int w=16*s, h=8*s;
  if(w<2) { display.drawPixel(x,y,1); return; }
  display.fillRect(x-w/2, y-h, w, h, 1);
  if(w>6) {
    display.fillRect(x-w/2, y-h/2, w/5, h/2, 0); 
    display.fillRect(x+w/2-w/5, y-h/2, w/5, h/2, 0); 
    display.fillRect(x-w/4, y-h+s, w/2, h/4, 0); 
  }
}

void runRaceGame() {
  if(r_gameOver) {
    display.clearDisplay();
    display.setCursor(30,20); display.setTextSize(2); display.print("CRASH!");
    display.setCursor(30,45); display.setTextSize(1); display.print("RANK: "); display.print(r_rank);
    display.display();
    if(digitalRead(BTN_ACT)==LOW) { r_setup(); delay(500); }
    return;
  }

  // レースBGM
  int tempo = 120 - (r_speed*10); if(tempo<80) tempo=80;
  if(millis() - r_lastNoteTime > tempo) {
    r_lastNoteTime = millis();
    int note = pgm_read_word_near(bgm_melody_race + r_bgmIndex);
    tone(SPK_PIN, note, tempo*0.9);
    r_bgmIndex = (r_bgmIndex + 1) % 16;
  }

  bool L = !digitalRead(BTN_LEFT), R = !digitalRead(BTN_RIGHT), T = !digitalRead(BTN_ACT);
  if(L && r_playerX > -1.5) r_playerX -= 0.06 + r_speed*0.02;
  if(R && r_playerX < 1.5) r_playerX += 0.06 + r_speed*0.02;
  if(T) { if(r_speed<3.0) r_speed+=0.1; } else { if(r_speed>0) r_speed-=0.04; }
  if(r_speed<0) r_speed=0;
  r_roadOffset += r_speed*1.5; if(r_roadOffset>30) r_roadOffset=0;

  display.clearDisplay();
  int H = 24; int VX = 64 - (int)(r_playerX * 40);
  display.drawLine(0, H, 128, H, 1);
  display.drawLine(VX, H, 0-(int)(r_playerX*100), 64, 1);
  display.drawLine(VX, H, 128-(int)(r_playerX*100), 64, 1);
  
  for(float z=100; z>1; z-=20) {
    float dz = z - r_roadOffset; 
    while(dz<5) dz+=100; while(dz>105) dz-=100;
    int ly = H + (int)(500/(dz+10));
    if(ly>=H && ly<64) display.drawFastVLine(VX, ly, 200/(dz+10), 1);
  }

  if(random(100)<5 && r_speed>1.0) {
    for(int i=0;i<3;i++) if(!r_enemies[i].active) {
      r_enemies[i]={ (random(3)-1)*1.5f + random(-50,50)/100.0f, 200, true }; break;
    }
  }
  for(int i=0;i<3;i++) if(r_enemies[i].active) {
    r_enemies[i].z -= r_speed;
    if(r_enemies[i].z < 5) {
      r_enemies[i].active=false; if(r_rank>1) { r_rank--; tone(SPK_PIN,1500,50); }
    } else {
      float s = 60.0/(r_enemies[i].z+10);
      int ex = VX + (int)(r_enemies[i].x * s * 20);
      int ey = H + (int)(500/(r_enemies[i].z+10));
      if(ey>=H) {
        drawRaceCar(ex, ey, s);
        if(r_enemies[i].z < 15 && abs(r_playerX - r_enemies[i].x) < 0.6) {
          r_gameOver=true; noTone(SPK_PIN); tone(SPK_PIN,100,500);
        }
      }
    }
  }

  int b = (r_speed>1.5 && millis()%60<30)?1:0;
  display.drawBitmap(56, 56-b, race_car_sprite, 16, 8, 1);
  display.setCursor(90,0); display.print("POS:"); display.print(r_rank);
  display.drawFastHLine(5,5, (int)(r_speed*10), 1);
  display.display();
}
