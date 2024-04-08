all: np_simple.cpp
	g++ np_simple.cpp -o np_simple
	g++ np_single_proc.cpp -o np_single_proc
clean:
	rm np_simple
	rm np_single_proc