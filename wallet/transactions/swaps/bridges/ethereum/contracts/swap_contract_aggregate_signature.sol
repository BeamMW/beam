// SPDX-License-Identifier: UNLICENSED
pragma solidity >=0.6.0 <0.8.0;

contract AtomicSwap {

    struct Swap {
        uint refundTimeInBlocks;
        address initiator;
        address participant;
        uint256 value;
    }

    mapping(address => Swap) swaps;
    
    // event for EVM logging
    // TODO

    modifier isNotInitiated(uint refundTimeInBlocks, address hashedSecret, address participant) {
        require(swaps[hashedSecret].refundTimeInBlocks == 0, "swap for this hash is already initiated");
        require(participant != address(0), "invalid participant address");
        require(block.number < refundTimeInBlocks, "refundTimeInBlocks has already come");
        _;
    }

    modifier isRefundable(address hashedSecret) {
        require(block.number >= swaps[hashedSecret].refundTimeInBlocks);
        require(msg.sender == swaps[hashedSecret].initiator);
        _;
    }
    
    modifier isRedeemable(address hashedSecret) {
        require(msg.sender == swaps[hashedSecret].participant, "invalid msg.sender");
        _;
    }
    
    function initiate(uint refundTimeInBlocks, address hashedSecret, address participant) public
        payable 
        isNotInitiated(refundTimeInBlocks, hashedSecret, participant)
    {
        swaps[hashedSecret].refundTimeInBlocks = refundTimeInBlocks;
        swaps[hashedSecret].participant = participant;
        swaps[hashedSecret].initiator = msg.sender;
        swaps[hashedSecret].value = msg.value;
    }
    
    function redeem(address hashedSecret, bytes32 r, bytes32 s, uint8 v) public
        isRedeemable(hashedSecret)
    {
        if (v != 27 && v != 28) {
            revert("invalid signature 'v' value");
        }

        bytes32 hash = keccak256(abi.encodePacked(hashedSecret, swaps[hashedSecret].participant, swaps[hashedSecret].initiator, swaps[hashedSecret].refundTimeInBlocks));

        // If the signature is valid (and not malleable), return the signer address
        address signer = ecrecover(hash, v, r, s);
        require(signer != address(0), "invalid signature");
        require(signer == hashedSecret, "invalid address");

        Swap memory tmp = swaps[hashedSecret];
        delete swaps[hashedSecret];
        
       payable(tmp.participant).transfer(tmp.value);
    }

    function refund(address hashedSecret) public
        isRefundable(hashedSecret) 
    {
        Swap memory tmp = swaps[hashedSecret];
        delete swaps[hashedSecret];

        payable(tmp.initiator).transfer(tmp.value);
    }

    function getSwapDetails(address hashedSecret)
    public view returns (uint refundTimeInBlocks, address initiator, address participant, uint256 value)
    {
        refundTimeInBlocks = swaps[hashedSecret].refundTimeInBlocks;
        initiator = swaps[hashedSecret].initiator;
        participant = swaps[hashedSecret].participant;
        value = swaps[hashedSecret].value;
    }
}