all:
	g++ --std=c++20 -o ges -O3 main.cpp -lcurl -lspdlog -lfmt

debug:
	g++ --std=c++20 -o ges-debug -O0 -g main.cpp -lcurl -lspdlog -lfmt

clean:
	rm ges
