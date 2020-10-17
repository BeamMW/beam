	This is the demonstration of the beam shader (a.k.a. contract) ecosystem.

common.h:

	Contains declarations for common types and environment support functions for shaders (provided by the beam VM).
	Should be included in all the shaders


vault.h/cpp:
	
	A simple 'vault' shader. Emulates account-based value transfer. Exports the following methods:
		0: Ctor - constructor (empty)
		1: Dtor - destructor (empty)
		2: Deposit. Updates the target account info, consumes the specified amount fom the transaction balance.
		3: Withdraw. Updates the target account info, releases the specified amount fom the transaction balance. Demands appropriate account signature to be included in the transaction.

	Methods 0, 1 are always reserved for Ctor and Dtor. Must be provided even if they're not necessary.
	By convention all exported methods should have a single pointer/reference parameter, and no return type. Hence all the in/outs must be packed in a single struct.

	Hence the invocation parameters (exact struct definition, and method number) are described in a separate header (.h) file. This way it can also be included in other projects (host or other shader).

	Uses the following environment functions:
		- SaveVar - store arbitrary data for the specific key (within this shader storage, different shaders using  the same key will access different data)
		- LoadVar - retrieve the data for the specific key
			Those 2 are used to manage the requested account info.
		- Halt - stop (abort) shader execution. Indicates an error.
		- FundsLock - consume specific amount from the transaction balance
		- FundsUnlock - release specific amount to the transaction balance.
			In addition to the shader logic, VM tracks the overall amount locked by the shader. Attempt to unlock excessive amount will halt the shader.
		- AddSig - instructs VM to include an additional pubkey in the transaction signature verification.
	

	To compile a shader into WebAssembly use the following:

clang -O3 --target=wasm32 -Wl,--export-dynamic,--no-entry,--allow-undefined -nostdlib --output {output_file}.wasm {source_files}

	*NOTE*: earlier we used --shared instead of --allow-undefined, which should build a proper shared library.
	But there is a bug in llvm atm, the build fails if static const arrays are used (strings, predefined data buffers, etc.)
	

	The result is a binary wasm file. The textual view (for debugging) is available here: https://webassembly.github.io/wabt/demo/wasm2wat/
