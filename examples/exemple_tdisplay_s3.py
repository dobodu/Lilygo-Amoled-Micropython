
import random
import utime
import amoled
import tft_config_tdisplay_s3
import fonts.large as font

tft = tft_config_tdisplay_s3.config()

POLYGON = [(50,-25),(25,-25),(25,-50),(-25,-50),(-25,-25),(-50,-25),(-50,25),(-25,25),(-25,50),(25,50),(25,25),(50,25)]

def main():
    tft_config_tdisplay_s3.TFT_CDE.value(1)
    tft.reset()
    tft.init()
    tft.rotation(3)
    tft.brightness(0)
    tft.jpg("/bmp/smiley_small.jpg",80,0)
    for i in range(255):
        tft.brightness(i)
        utime.sleep(0.02)
    utime.sleep(1)
    tft.fill(amoled.BLACK)
    utime.sleep(1)
    
    text = "Hello!"
    text_length = tft.write_len(font,text)
    text_height = font.HEIGHT

    while True:

        for rotation in range(4):
            tft.rotation(rotation)
            tft.fill(amoled.BLACK)
            col_max = tft.width()
            row_max = tft.height()
            text_xmax = col_max - text_length
            text_ymax = row_max - text_height
            
            filled = random.randint(0,1)
            kind = random.randint(0,5)
            
            start_time = utime.ticks_ms()

            for _ in range(128):
                xpos = random.randint(0, col_max)
                ypos = random.randint(0, row_max)
                length = random.randint(0, col_max - xpos) // 2
                height = random.randint(0, row_max - ypos) // 2
                radius = random.randint(0, min(col_max - xpos, xpos, row_max - ypos, ypos )) // 2
                angle = random.randint(0,628) / 100
                color = tft.colorRGB(
                        random.getrandbits(8),
                        random.getrandbits(8),
                        random.getrandbits(8))
                color2 = tft.colorRGB(
                        random.getrandbits(8),
                        random.getrandbits(8),
                        random.getrandbits(8))
                
                if kind == 0 :
                    if filled :
                        tft.fill_circle(xpos,ypos,radius, color)
                    else :
                        tft.circle(xpos, ypos, radius, color)
                
                if kind == 1 :
                    if filled :
                        tft.fill_rect(xpos,ypos,length, height, color)
                    else :
                        tft.rect(xpos,ypos,length, height, color)
                
                if kind == 2 :
                    if filled :
                        tft.fill_bubble_rect(xpos,ypos,length, height, color)
                    else :
                        tft.bubble_rect(xpos,ypos,length, height, color)
                        
                if kind == 3 :
                    if filled :
                        tft.fill_trian(random.randint(0, col_max), random.randint(0, row_max),
                                       random.randint(0, col_max), random.randint(0, row_max),
                                       random.randint(0, col_max), random.randint(0, row_max), color)
                    else :
                        tft.trian(random.randint(0, col_max), random.randint(0, row_max),
                                       random.randint(0, col_max), random.randint(0, row_max),
                                       random.randint(0, col_max), random.randint(0, row_max), color)
                        
                if kind == 4 :
                    if filled :
                        tft.write(font,text, random.randint(10, text_xmax-10), random.randint(10, text_ymax-10), color,color2)
                    else :
                        tft.write(font,text, random.randint(10, text_xmax-10), random.randint(10, text_ymax-10), color)
                        
                if kind == 5 :
                    if filled :
                        tft.fill_polygon(POLYGON, random.randint(55, col_max-55), random.randint(55, row_max-55) , color, angle)
                    else :
                        tft.polygon(POLYGON, random.randint(55, col_max-55), random.randint(55, row_max-55) , color, angle)
            
            end_time = utime.ticks_ms()
            fps = 1000*128/(end_time - start_time)
            fps_txt = "Rot {:.0f} - {:.0f}/s".format(rotation,fps)
            tft.write(font,fps_txt, 0, 0, color)
            print(rotation, kind, filled, fps) 
            
            utime.sleep(2)
       
main()
