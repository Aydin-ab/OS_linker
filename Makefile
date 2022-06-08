mmy: linker.cpp
	bash -c "module load gcc-9.2"
	g++ -std=c++11 -g linker.cpp -o linker

clean:
	rm -f linker *~