scr-benchmark: main.cc
	@g++ -g -std=gnu++11 -o $@ $^ -lz

run:
	./scr-benchmark ./test.rep

test:
	gdb --args ./scr-benchmark ./test.rep
