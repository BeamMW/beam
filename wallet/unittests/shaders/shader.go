package main

func Func1(a int) int { // В го жестко задается форматирование, например эту скобку нельзя вниз, так то осторожно
	return a * 2
}

func Func2(a int) int {
	var j int = 19
	Func1(a)
	Func2(a)
	return a + j
}

// в го нет референсов, ток указатели
func Func3(a *int) {
	*a = 3 * Func2(*a)
}

type A struct{
    B int
    C int
}

// пустой interface{} - фактически указатель на любой тип (void*)
//но с возможностью достать инфу о типе
//export Ctor 
func Ctor(a *A) {
	var b int = a.B
	b += 10

	var g int = b
	var c int = g

	b = g + 12*c
	Func3(&b)
}

//export Dtor
func Dtor(any interface{}) {
}

//export Method_2
func Method_2() {

}

//export Method_3
func Method_3(a int) {

}


func main() {
	// пакет main требует func main
}
