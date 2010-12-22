objects = pigmap.o blockimages.o chunk.o map.o render.o rgba.o tables.o utils.o

pigmap : $(objects)
	g++ $(objects) -o pigmap -l z -l png -l pthread -O3

pigmap.o : pigmap.cpp blockimages.h chunk.h map.h render.h rgba.h tables.h utils.h
	g++ -c pigmap.cpp -O3
blockimages.o : blockimages.cpp blockimages.h map.h rgba.h utils.h
	g++ -c blockimages.cpp -O3
chunk.o : chunk.cpp blockimages.h chunk.h map.h render.h rgba.h tables.h utils.h
	g++ -c chunk.cpp -O3
map.o : map.cpp map.h utils.h
	g++ -c map.cpp -O3
render.o : render.cpp blockimages.h chunk.h map.h render.h rgba.h tables.h utils.h
	g++ -c render.cpp -O3
rgba.o : rgba.cpp rgba.h utils.h
	g++ -c rgba.cpp -O3
tables.o : tables.cpp map.h tables.h utils.h
	g++ -c tables.cpp -O3
utils.o : utils.cpp utils.h
	g++ -c utils.cpp -O3

clean :
	rm -f *.o pigmap
	