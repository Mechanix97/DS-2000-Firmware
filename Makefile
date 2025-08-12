compile:
	platformio.exe run 

upload: compile
	copy ".pio\build\pico\firmware.uf2" E:\

clean:
	platformio.exe run --target clean 
