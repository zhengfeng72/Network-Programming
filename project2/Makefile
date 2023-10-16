all:
	g++ np_simple.cpp -o np_simple 
	g++ np_single_proc.cpp -o np_single_proc
	g++ np_multi_proc.cpp -o np_multi_proc

simple: np_simple.cpp
	g++ np_simple.cpp -o np_simple 

single: np_single_proc.cpp
	g++ np_single_proc.cpp -o np_single_proc

multi: np_multi_proc.cpp
	g++ np_multi_proc.cpp -o np_multi_proc

clean:
	rm np_simple np_single_proc np_multi_proc 
