// Exact E32R32P ST7789P3 initialization sequence from the vendor's V1.0
// Arduino package. Source: https://www.lcdwiki.com/3.2inch_ESP32-32E_Display
// Kept separate so PlatformIO can patch TFT_eSPI 2.5.43 reproducibly.
{
  writecommand(0x36);
  writedata(0x00);

  writecommand(0x3A);
  writedata(0x05);

  writecommand(0xB2);
  writedata(0x0C);
  writedata(0x0C);
  writedata(0x00);
  writedata(0x33);
  writedata(0x33);

  writecommand(0xB7);
  writedata(0x74);

  writecommand(0xBB);
  writedata(0x13);

  writecommand(0xC0);
  writedata(0x2C);

  writecommand(0xC2);
  writedata(0x01);

  writecommand(0xC3);
  writedata(0x10);

  writecommand(0xC4);
  writedata(0x20);

  writecommand(0xC6);
  writedata(0x0F);

  writecommand(0xD0);
  writedata(0xA4);
  writedata(0xA1);

  writecommand(0xD6);
  writedata(0xA1);

  writecommand(0xE0);
  writedata(0xD0);
  writedata(0x07);
  writedata(0x0E);
  writedata(0x0B);
  writedata(0x0A);
  writedata(0x14);
  writedata(0x38);
  writedata(0x33);
  writedata(0x4F);
  writedata(0x37);
  writedata(0x16);
  writedata(0x16);
  writedata(0x2A);
  writedata(0x2E);

  writecommand(0xE1);
  writedata(0xD0);
  writedata(0x0B);
  writedata(0x10);
  writedata(0x08);
  writedata(0x08);
  writedata(0x06);
  writedata(0x35);
  writedata(0x54);
  writedata(0x4D);
  writedata(0x0A);
  writedata(0x14);
  writedata(0x14);
  writedata(0x2C);
  writedata(0x2F);

  writecommand(0xE9);
  writedata(0x11);
  writedata(0x11);
  writedata(0x03);

  writecommand(0x21);
  writecommand(0x11);

  end_tft_write();
  delay(120);
  begin_tft_write();

  writecommand(0x29);
  writecommand(0x2C);

#ifdef TFT_BL
  digitalWrite(TFT_BL, HIGH);
  pinMode(TFT_BL, OUTPUT);
#endif
}
