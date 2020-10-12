// SPDX-License-Identifier: UNLICENSED
pragma solidity >=0.6.0 <0.8.0;

contract AtomicSwap {

    enum State { Empty, Locked, Withdrawed }

    struct Swap {
        uint initBlockNumber;
        uint refundTimeInBlocks;
        address hashedSecret;
        address initiator;
        address participant;
        uint256 value;
        State state;
    }

    mapping(address => Swap) swaps;
    
    // event for EVM logging
    // TODO

    modifier isNotInitiated(address hashedSecret) {
        require(swaps[hashedSecret].state == State.Empty);
        _;
    }

    modifier isRefundable(address hashedSecret) {
        require(block.number > swaps[hashedSecret].initBlockNumber + swaps[hashedSecret].refundTimeInBlocks);
        require(swaps[hashedSecret].state == State.Locked);
        require(msg.sender == swaps[hashedSecret].initiator);
        _;
    }
    
    modifier isRedeemable(address hashedSecret) {
        require(swaps[hashedSecret].state == State.Locked, "invalid State");
        require(msg.sender == swaps[hashedSecret].participant, "invalid msg.sender");
        require(block.number < swaps[hashedSecret].initBlockNumber + swaps[hashedSecret].refundTimeInBlocks, "too late");
        _;
    }
    
    function initiate(uint refundTimeInBlocks, address hashedSecret, address participant) public
        payable 
        isNotInitiated(hashedSecret)
    {
        swaps[hashedSecret].refundTimeInBlocks = refundTimeInBlocks;
        swaps[hashedSecret].initBlockNumber = block.number;
        swaps[hashedSecret].hashedSecret = hashedSecret;
        swaps[hashedSecret].participant = participant;
        swaps[hashedSecret].initiator = msg.sender;
        swaps[hashedSecret].state = State.Locked;
        swaps[hashedSecret].value = msg.value;
    }
    
    function redeem(address hashedSecret, bytes32 r, bytes32 s, uint8 v) public
        isRedeemable(hashedSecret)
    {
        if (v != 27 && v != 28) {
            revert("invalid signature 'v' value");
        }

        bytes32 hash = keccak256(abi.encodePacked(swaps[hashedSecret].hashedSecret, swaps[hashedSecret].participant, swaps[hashedSecret].initiator));

        // If the signature is valid (and not malleable), return the signer address
        address signer = ecrecover(hash, v, r, s);
        require(signer != address(0), "invalid signature");
        require(signer == swaps[hashedSecret].hashedSecret, "invalid address");
        
        payable(swaps[hashedSecret].participant).transfer(swaps[hashedSecret].value);
        swaps[hashedSecret].state = State.Withdrawed;
        // TODO: event Redeemed(block.timestamp);
    }

    function refund(address hashedSecret) public
        isRefundable(hashedSecret) 
    {
        payable(swaps[hashedSecret].initiator).transfer(swaps[hashedSecret].value);
        swaps[hashedSecret].state = State.Withdrawed;
        // TODO: event Refunded(block.timestamp);
    }
    
    function getSwapDetails(address hashedSecret) public view returns (uint initBlockNumber, uint256 value)
    {
        if (swaps[hashedSecret].state == State.Locked) {
            initBlockNumber = swaps[hashedSecret].initBlockNumber;
            value = swaps[hashedSecret].value;
        }
    }
    
}