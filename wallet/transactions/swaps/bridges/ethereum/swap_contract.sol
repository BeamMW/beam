// SPDX-License-Identifier: UNLICENSED
pragma solidity >=0.6.0 <0.8.0;

contract AtomicSwap {

    enum State { Empty, Locked, Withdrawed }

    struct Swap {
        uint initBlockNumber;
        uint refundTimeInBlocks;
        bytes32 hashedSecret;
        address initiator;
        address participant;
        uint256 value;
        State state;
    }

    mapping(bytes32 => Swap) swaps;
    
    // event for EVM logging
    // TODO

    modifier isNotInitiated(bytes32 hashedSecret) {
        require(swaps[hashedSecret].state == State.Empty);
        _;
    }

    modifier isRefundable(bytes32 hashedSecret) {
        require(block.number > swaps[hashedSecret].initBlockNumber + swaps[hashedSecret].refundTimeInBlocks);
        require(swaps[hashedSecret].state == State.Locked);
        require(msg.sender == swaps[hashedSecret].initiator);
        _;
    }
    
    modifier isRedeemable(bytes32 hashedSecret, bytes32 secret) {
        require(swaps[hashedSecret].state == State.Locked, "invalid State");
        require(msg.sender == swaps[hashedSecret].participant, "invalid msg.sender");
        require(block.number < swaps[hashedSecret].initBlockNumber + swaps[hashedSecret].refundTimeInBlocks);
        require(sha256(abi.encodePacked(secret)) == hashedSecret, "invalid secret");
        _;
    }
    
    function initiate(uint refundTimeInBlocks, bytes32 hashedSecret, address participant) public
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
    
    function redeem(bytes32 secret, bytes32 hashedSecret) public
        isRedeemable(hashedSecret, secret)
    {
        payable(swaps[hashedSecret].participant).transfer(swaps[hashedSecret].value);
        swaps[hashedSecret].state = State.Withdrawed;
        // TODO: event Redeemed(block.timestamp);
    }

    function refund(bytes32 hashedSecret) public
        isRefundable(hashedSecret) 
    {
        payable(swaps[hashedSecret].initiator).transfer(swaps[hashedSecret].value);
        swaps[hashedSecret].state = State.Withdrawed;
        // TODO: event Refunded(block.timestamp);
    }
    
    function getSwapDetails(bytes32 hashedSecret) public view returns (uint initBlockNumber, uint256 value)
    {
        if (swaps[hashedSecret].state == State.Locked) {
            initBlockNumber = swaps[hashedSecret].initBlockNumber;
            value = swaps[hashedSecret].value;
        }
    }
}