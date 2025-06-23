compile:
	platformio.exe run 

upload: compile
	copy ".pio\build\pico\firmware.uf2" F:\

clean:
	platformio.exe run --target clean 
