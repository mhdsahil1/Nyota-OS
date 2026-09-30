from PIL import Image, ImageDraw, ImageFont

# Generate a high-quality 8x16 monospace font table
font_path = "C:/Windows/Fonts/consola.ttf"
try:
    font = ImageFont.truetype(font_path, 13)
except Exception:
    font = ImageFont.load_default()

lines = []
lines.append("/* Nyota OS 8x16 Monospace Bitmap Font */")
lines.append("#ifndef NYOTA_FONT8X16_H")
lines.append("#define NYOTA_FONT8X16_H")
lines.append("")
lines.append('#include "types.h"')
lines.append("")
lines.append("static const uint8_t font8x16[256][16] = {")

for c in range(256):
    img = Image.new("1", (8, 16), color=0)
    draw = ImageDraw.Draw(img)
    ch = chr(c) if 32 <= c <= 126 else " "
    draw.text((0, 0), ch, font=font, fill=1)
    
    bytes_list = []
    for y in range(16):
        byte_val = 0
        for x in range(8):
            if img.getpixel((x, y)):
                byte_val |= (1 << (7 - x))
        bytes_list.append(f"0x{byte_val:02X}")
    comment = f'/* {c}: "{ch}" */' if ch != " " else f"/* {c} */"
    lines.append("    {" + ", ".join(bytes_list) + "}, " + comment)

lines.append("};")
lines.append("")
lines.append("#endif /* NYOTA_FONT8X16_H */")

with open("include/drivers/font8x16.h", "w") as f:
    f.write("\n".join(lines) + "\n")
print("font8x16.h generated successfully")
