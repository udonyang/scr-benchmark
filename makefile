scr-benchmark: main.cc
	@g++ -std=gnu++11 -o $@ $^

run:
	./scr-benchmark ./test.rep
