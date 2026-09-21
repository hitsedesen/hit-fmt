namespace sample::formatting {

class Derived:public Base<FirstType,SecondType>,private Other
{
public:
Derived(int first,int second):Base<FirstType,SecondType>(first),Second_(second)
{
if(first>second){Second_=first;}
}
private:
int Second_;
};

void Visit(EMode mode)
{
    switch (mode) {
        case EMode::Keep:
            return;
        // Forward is interpreted as Fail.
        case EMode::Forward:
            if (Ready()) {
                Run();
                return;
            }
        [[fallthrough]];
        case EMode::Fail:
            break;
    }
}

}
