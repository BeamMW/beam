
#include "beamHashIII.h"


namespace sipHash {
 
static uint64_t rotl(uint64_t x, uint64_t b) {
	return (x << b) | (x >> (64 - b));
}

#define sipRound() {		\
	v0 += v1; v2 += v3;	\
	v1 = rotl(v1,13);	\
	v3 = rotl(v3,16); 	\
	v1 ^= v0; v3 ^= v2;	\
	v0 = rotl(v0,32); 	\
	v2 += v1; v0 += v3;	\
	v1 = rotl(v1,17);   	\
	v3 = rotl(v3,21);	\
	v1 ^= v2; v3 ^= v0; 	\
	v2 = rotl(v2,32);	\
}

uint64_t siphash24(uint64_t state0, uint64_t state1, uint64_t state2, uint64_t state3, uint64_t nonce) {
	uint64_t v0, v1, v2, v3;

	v0 = state0; v1=state1; v2=state2; v3=state3;
	v3 ^= nonce;
	sipRound();
	sipRound();
	v0 ^= nonce;
   	v2 ^= 0xff;
	sipRound();
	sipRound();
	sipRound();
	sipRound();

	return (v0 ^ v1 ^ v2 ^ v3);	
}

} //end namespace sipHash


stepElem::stepElem(const uint64_t * prePow, uint32_t index) {
	workBits.reset();

	for (int32_t i=6; i>=0; i--) {
		workBits = (workBits << 64);
		uint64_t hash=sipHash::siphash24(prePow[0],prePow[1],prePow[2],prePow[3],(index << 3)+i);
		workBits |= hash; 		
	}

	indexTree.assign(1, index);
}

stepElem::stepElem(const stepElem &a, const stepElem &b, uint32_t remLen) {
	// Create a new rounds step element from matching two ancestors
	workBits.reset();

	workBits = a.workBits ^ b.workBits;
	workBits = (workBits >> collisionBitSize);

	std::bitset<workBitSize> mask;
	mask.set();
	mask = (mask >> (workBitSize-remLen));
	workBits &= mask;

	if (a.indexTree[0] < b.indexTree[0]) {
		indexTree.insert(indexTree.end(), a.indexTree.begin(), a.indexTree.end());
		indexTree.insert(indexTree.end(), b.indexTree.begin(), b.indexTree.end());
	} else {
		indexTree.insert(indexTree.end(), b.indexTree.begin(), b.indexTree.end());
		indexTree.insert(indexTree.end(), a.indexTree.begin(), a.indexTree.end());
	} 
}

void stepElem::applyMix(uint32_t remLen) {
	std::bitset<512> tempBits(workBits.to_string());

	// Add in the bits of the index tree to the end of work bits
	uint32_t padNum = ((512-remLen) + collisionBitSize) / (collisionBitSize + 1);
	padNum = std::min<uint32_t>(padNum, indexTree.size());

	for (uint32_t i=0; i<padNum; i++) {
		std::bitset<512> tmp(indexTree[i]);
		tmp = tmp << (remLen+i*(collisionBitSize + 1));
		tempBits |= tmp;
	}


	// Applyin the mix from the lined up bits
	std::bitset<512> mask(0xFFFFFFFFFFFFFFFFUL);
	uint64_t result = 0;
	for (uint32_t i=0; i<8; i++) {
		uint64_t tmp = (tempBits & mask).to_ulong();
		tempBits = tempBits >> 64;

		result += sipHash::rotl(tmp, (29*(i+1)) & 0x3F);
	}
	result = sipHash::rotl(result, 24);


	// Wipe out lowest 64 bits in favor of the mixed bits
	workBits = (workBits >> 64);
	workBits = (workBits << 64);
	workBits |= std::bitset<workBitSize>(result);
}

uint32_t stepElem::getCollisionBits() const {
	std::bitset<workBitSize> mask((1 << collisionBitSize) - 1);
	return (uint32_t) (workBits & mask).to_ulong();
}

bool stepElem::isZero() {
	return workBits.none();
}

uint64_t getLowBits(stepElem test) {
	std::bitset<workBitSize> mask(~0);
	return (uint64_t) (test.workBits & mask).to_ulong();
}
/********

    Friend Functions to compare step elements

********/


bool hasCollision(stepElem &a, stepElem &b) {
	return (a.getCollisionBits() == b.getCollisionBits());
}

bool distinctIndices(stepElem &a, stepElem &b) {
	for (uint32_t indexA : a.indexTree) {
		for (uint32_t indexB : b.indexTree) {
			if (indexA == indexB) return false;
		}
	}
	return true;
}

bool indexAfter(stepElem &a, stepElem &b) {
	return (a.indexTree[0] < b.indexTree[0]);
}

bool sortStepElement(const stepElem &a, const stepElem &b) {
	return (a.getCollisionBits() < b.getCollisionBits());
}


/********

    Beam Hash III Verify Functions & CPU Miner

********/

std::vector<uint32_t> GetIndicesFromMinimal(std::vector<uint8_t> soln) {
	std::bitset<800> inStream;
	std::bitset<800> mask((1 << (collisionBitSize+1))-1);

	inStream.reset();
	for (int32_t i = 99; i>=0; i--) {
		inStream = (inStream << 8);
		inStream |= (uint64_t) soln[i];
	}

	std::vector<uint32_t> res;
	for (uint32_t i=0; i<32; i++) {
		res.push_back((uint32_t) (inStream & mask).to_ulong() );
		inStream = (inStream >> (collisionBitSize+1));
	}

	return res;
}

std::vector<uint8_t> GetMinimalFromIndices(std::vector<uint32_t> sol) {
	std::bitset<800> inStream;
	std::bitset<800> mask(0xFF);

	inStream.reset();
	for (int32_t i = sol.size(); i>=0; i--) {
		inStream = (inStream << (collisionBitSize+1));
		inStream |= (uint64_t) sol[i];
	}

	std::vector<uint8_t> res;
	for (uint32_t i=0; i<100; i++) {
		res.push_back((uint8_t) (inStream & mask).to_ulong() );
		inStream = (inStream >> 8);
	}

	return res;
}

int BeamHash_III::InitialiseState(blake2b_state& base_state) {
	unsigned char personalization[BLAKE2B_PERSONALBYTES] = {};
	memcpy(personalization, "Beam-PoW", 8);
	memcpy(personalization+8,  &workBitSize, 4);
	memcpy(personalization+12, &numRounds, 4);

	const uint8_t outlen = 32;

	blake2b_param param = {0};
	param.digest_length = outlen;
	param.fanout = 1;
	param.depth = 1;

	memcpy(&param.personal, personalization, BLAKE2B_PERSONALBYTES);
	return blake2b_init_param(&base_state, &param);
}


bool BeamHash_III::IsValidSolution(const blake2b_state& base_state, std::vector<uint8_t> soln) {
	
	if (soln.size() != 104)  {		
		return false;
    	}
	
	uint64_t prePow[4];
	blake2b_state state = base_state;
	// Last 4 bytes of solution are our extra nonce
	blake2b_update(&state, (uint8_t*) &soln[100], 4);			
	blake2b_final(&state, (uint8_t*) &prePow[0], static_cast<uint8_t>(32));	

	// This will only evaluate bytes 0..99
	std::vector<uint32_t> indices = GetIndicesFromMinimal(soln);

	std::vector<stepElem> X;
	for (uint32_t i=0; i<indices.size(); i++) {
		X.emplace_back(&prePow[0], indices[i]);
	}

	uint32_t round=1;
	while (X.size() > 1) {
		std::vector<stepElem> Xtmp;

		for (size_t i = 0; i < X.size(); i += 2) {
			uint32_t remLen = workBitSize-(round-1)*collisionBitSize;
			if (round == 5) remLen -= 64;

			X[i].applyMix(remLen);
			X[i+1].applyMix(remLen);
		
			if (!hasCollision(X[i], X[i+1])) { 
				//std::cout << "Collision Error" << i << " " << X.size() << " " << X[i].getCollisionBits() << " " << X[i+1].getCollisionBits() << std::endl;
                		return false;
            		}

			if (!distinctIndices(X[i], X[i+1])) { 
				//std::cout << "Non-Distinct" << i << " " << X.size() << std::endl;
                		return false;
            		}

			if (!indexAfter(X[i], X[i+1])) { 
				//std::cout << "Index Order" << i << " " << X.size() << std::endl;
                		return false;
            		}

			remLen = workBitSize-round*collisionBitSize;
			if (round == 4) remLen -= 64;
			if (round == 5) remLen = collisionBitSize;

			Xtmp.emplace_back(X[i], X[i+1], remLen);
		}

		X = Xtmp;
		round++;
	}

	return X[0].isZero();	
}


SolverCancelledException beamSolverCancelled;

bool BeamHash_III::OptimisedSolve(const blake2b_state& base_state,
                                 const std::function<bool(const std::vector<unsigned char>&)> validBlock,
                                 const std::function<bool(SolverCancelCheck)> cancelled) {

	uint64_t prePow[4];
	blake2b_state state = base_state;
	
	uint8_t extraNonce[4] = {0};

	blake2b_update(&state, (uint8_t*) &extraNonce, 4);			
	blake2b_final(&state, (uint8_t*) &prePow[0], static_cast<uint8_t>(32));

	std::vector<stepElem> elements;
	elements.reserve(1 << (collisionBitSize+1)); 

	// Seeding
	for (uint32_t i=0; i<(1 << (collisionBitSize+1)); i++) {
		elements.emplace_back(&prePow[0], i);
		if (cancelled(ListGeneration)) throw beamSolverCancelled;
	}

	// Round 1 to 5
	uint32_t round;
	for (round=1; round<5; round++) {

		uint32_t remLen = workBitSize-(round-1)*collisionBitSize;

		// Mixing of elements
		for (uint32_t i=0; i<elements.size(); i++) {
			elements[i].applyMix(remLen);
			if (cancelled(MixElements)) throw beamSolverCancelled;
		}

		// Sorting
		std::sort(elements.begin(), elements.end(), sortStepElement);
		if (cancelled(ListSorting)) throw beamSolverCancelled;

		// Set length of output bits
		remLen = workBitSize-round*collisionBitSize;
		if (round == 4) remLen -= 64;

		// Creating matches
		std::vector<stepElem> outElements;
		outElements.reserve(1 << (collisionBitSize+1));

		for (uint32_t i=0; i<elements.size()-1; i++) {
			uint32_t j=i+1;
			while (j < elements.size()) {
				if (hasCollision(elements[i], elements[j])) {
					outElements.emplace_back(elements[i], elements[j], remLen);
				} else {
					break;
				}
				j++;
			}
			if (cancelled(ListColliding)) throw beamSolverCancelled;
		}

		elements = outElements;	
	}

	// Check the output of the last round for solutions
	uint32_t remLen = workBitSize-(round-1)*collisionBitSize - 64;

	// Mixing of elements
	for (uint32_t i=0; i<elements.size(); i++) {
		elements[i].applyMix(remLen);
		if (cancelled(MixElements)) throw beamSolverCancelled;
	}

	// Sorting
	std::sort(elements.begin(), elements.end(), sortStepElement);
	if (cancelled(ListSorting)) throw beamSolverCancelled;

	// Set length of output bits
	remLen = collisionBitSize;

	// Creating matches
	for (uint32_t i=0; i<elements.size()-1; i++) {
		uint32_t j=i+1;
		while (j < elements.size()) {
			if (hasCollision(elements[i], elements[j])) {
				stepElem temp(elements[i], elements[j], remLen);

				if (temp.isZero()) {
					std::vector<uint8_t> sol = GetMinimalFromIndices(temp.indexTree);

					// Adding the extra nonce
					for (uint32_t i=0; i<4; i++) sol.push_back(extraNonce[i]);

					if (validBlock(sol))  return true;
				}
			} else {
				break;
			}
			j++;
		}
		if (cancelled(ListColliding)) throw beamSolverCancelled;
	}

	return false;
}

