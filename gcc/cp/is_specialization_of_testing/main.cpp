#include <iostream>
#include <tuple>
using namespace std;

template<typename T>
class Frog {
	static_assert( !__is_specialization_of(Frog<int>,Frog) );
};

int main(void)
{
	Frog<double> var;
}

