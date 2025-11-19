#include <functional>  // invoke
#include <memory>
#include <tuple>
#include <type_traits>
#include <cstdio>      // puts
using std::puts;

struct Processor {
    virtual void Donkey(void) {}
    virtual void Process(void) = 0;
    template<typename T> T Chocolate(T) { puts("Processor Chocolate"); return T{}; }
};

struct BaseOfEditor {
    virtual int Edit(void) = 0;
};

struct Editor : BaseOfEditor {
    virtual void Donkey(void) {}
    template<typename T> T Chocolate(T) { puts("Editor Chocolate"); return T{}; }
};

void EditAndProcess( std::chimeric_ptr< std::allow_dynamic, std::nothrow_resolve, Processor, Editor> const p )
{
    int n1 = p->Edit();
    p->Process();
    p->Donkey();
    p->Chocolate(34);

    int n2 = std::invoke( &BaseOfEditor::Edit, p );
    
    std::tuple< decltype(p) > donkey{ p };
    int n3 = std::apply ( &BaseOfEditor::Edit, donkey );

    BaseOfEditor *p2 = p;
}

struct Emulator : Editor {

    struct EmulatorProcessor : Processor {
        void Process(void) override { puts("EmulatorProcessor Process"); }
    };

    EmulatorProcessor proc;

    int Edit(void) override { puts("Emulator Edit"); return 7; }

    operator Processor&(void) noexcept { return proc; }
};

struct Lizard : Emulator, Processor {
	  void Process(void) override { puts("Lizard Process"); }
};

int main(void)
{
    Lizard e;
    EditAndProcess( &static_cast<Emulator&>(e) );
}
