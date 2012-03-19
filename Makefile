objects = pigmap.o blockimages.o chunk.o map.o render.o region.o rgba.o tables.o utils.o world.o

pigmap : $(objects)
	g++ $(objects) -o pigmap -l z -l png -l pthread -O3

pigmap.o : pigmap.cpp blockimages.h chunk.h map.h render.h rgba.h tables.h utils.h world.h
	g++ -c pigmap.cpp -O3
blockimages.o : blockimages.cpp blockimages.h rgba.h utils.h
	g++ -c blockimages.cpp -O3
chunk.o : chunk.cpp chunk.h map.h region.h tables.h utils.h
	g++ -c chunk.cpp -O3
map.o : map.cpp map.h utils.h
	g++ -c map.cpp -O3
render.o : render.cpp blockimages.h chunk.h map.h render.h rgba.h tables.h utils.h
	g++ -c render.cpp -O3
region.o : region.cpp map.h region.h tables.h utils.h
	g++ -c region.cpp -O3
rgba.o : rgba.cpp rgba.h utils.h
	g++ -c rgba.cpp -O3
tables.o : tables.cpp map.h tables.h utils.h
	g++ -c tables.cpp -O3
utils.o : utils.cpp utils.h
	g++ -c utils.cpp -O3
world.o : world.cpp map.h region.h tables.h world.h
	g++ -c world.cpp -O3

clean :
	rm -f *.o pigmap
	