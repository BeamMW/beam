// Copyright (c) 2020 The Beam Team	

#ifndef BEAMHASH_H
#define BEAMHASH_H

#include <bitset>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <functional>
#include <memory>
#include <vector>
#include <algorithm>
#include <iostream>

#include "powScheme.h"

const uint32_t workBitSize=448;
const uint32_t collisionBitSize=24;
const uint32_t numRounds=5;

class stepElem {	
	friend class BeamHash_III;
	
	private:
	std::bitset<workBitSize> workBits;
	std::vector<uint32_t> indexTree;

	public: 
	stepElem(const uint64_t * prePow, uint32_t index);
	stepElem(const stepElem &a, const stepElem &b, uint32_t remLen);

	void applyMix(uint32_t remLen);
	uint32_t getCollisionBits() const;
	bool isZero();

	friend bool hasCollision(stepElem &a, stepElem &b);
	friend bool distinctIndices(stepElem &a, stepElem &b);  
	friend bool indexAfter(stepElem &a, stepElem &b);  
	friend uint64_t getLowBits(stepElem test);
};

class BeamHash_III : public PoWScheme {
	public:	
	int InitialiseState(blake2b_state& base_state);
	bool IsValidSolution(const blake2b_state& base_state, std::vector<unsigned char> soln);

	#ifdef ENABLE_MINING
	bool OptimisedSolve(const blake2b_state& base_state,
                        const std::function<bool(const std::vector<unsigned char>&)> validBlock,
                        const std::function<bool(SolverCancelCheck)> cancelled);
	#endif
};

#endif 
