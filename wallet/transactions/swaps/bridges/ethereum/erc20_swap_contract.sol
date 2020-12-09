// SPDX-License-Identifier: UNLICENSED
pragma solidity >=0.6.0 <0.8.0;

// From file: openzeppelin-contracts/contracts/math/SafeMath.sol
library SafeMath {
    function add(uint256 a, uint256 b) internal pure returns (uint256) {
        uint256 c = a + b;
        require(c >= a, "SafeMath: addition overflow");
        return c;
    }
    function sub(uint256 a, uint256 b) internal pure returns (uint256) {
        require(b <= a, "SafeMath: subtraction overflow");
        uint256 c = a - b;
        return c;
    }
}

// From file: openzeppelin-contracts/contracts/utils/Address.sol
library Address {
    function isContract(address account) internal view returns (bool) {
        uint256 size;
        // solium-disable-next-line
        assembly { size := extcodesize(account) }
        return size > 0;
    }
}

// File: openzeppelin-contracts/contracts/token/ERC20/SafeERC20.sol
library SafeERC20 {
    using SafeMath for uint256;
    using Address for address;

    function safeTransfer(IERC20 token, address to, uint256 value) internal {
        callOptionalReturn(token, abi.encodeWithSelector(token.transfer.selector, to, value));
    }

    function safeTransferFrom(IERC20 token, address from, address to, uint256 value) internal {
        callOptionalReturn(token, abi.encodeWithSelector(token.transferFrom.selector, from, to, value));
    }

    function safeApprove(IERC20 token, address spender, uint256 value) internal {
        require(
            (value == 0) || (token.allowance(address(this), spender) == 0),
            "SafeERC20: approve from non-zero to non-zero allowance"
        );
        callOptionalReturn(token, abi.encodeWithSelector(token.approve.selector, spender, value));
    }

    function safeIncreaseAllowance(IERC20 token, address spender, uint256 value) internal {
        uint256 newAllowance = token.allowance(address(this), spender).add(value);
        callOptionalReturn(token, abi.encodeWithSelector(token.approve.selector, spender, newAllowance));
    }

    function safeDecreaseAllowance(IERC20 token, address spender, uint256 value) internal {
        uint256 newAllowance = token.allowance(address(this), spender).sub(value);
        callOptionalReturn(token, abi.encodeWithSelector(token.approve.selector, spender, newAllowance));
    }

    function callOptionalReturn(IERC20 token, bytes memory data) private {
        require(address(token).isContract(), "SafeERC20: call to non-contract");

        (bool success, bytes memory returndata) = address(token).call(data);
        require(success, "SafeERC20: low-level call failed");

        if (returndata.length > 0) {
            require(abi.decode(returndata, (bool)), "SafeERC20: ERC20 operation did not succeed");
        }
    }
}

// File: openzeppelin-contracts/contracts/token/ERC20/IERC20.sol
interface IERC20 {
    function totalSupply() external view returns (uint256);
    function balanceOf(address account) external view returns (uint256);
    function transfer(address recipient, uint256 amount) external returns (bool);
    function allowance(address owner, address spender) external view returns (uint256);
    function approve(address spender, uint256 amount) external returns (bool);
    function transferFrom(address sender, address recipient, uint256 amount) external returns (bool);
    event Transfer(address indexed from, address indexed to, uint256 value);
    event Approval(address indexed owner, address indexed spender, uint256 value);
}


contract AtomicSwap {
    using SafeERC20 for IERC20;

    struct Swap {
        uint refundTimeInBlocks;
        address contractAddress;
        address initiator;
        address participant;
        uint256 value;
    }

    mapping(bytes32 => Swap) swaps;
    
    // event for EVM logging
    // TODO

    modifier isNotInitiated(uint refundTimeInBlocks, bytes32 hashedSecret, address participant) {
        require(swaps[hashedSecret].refundTimeInBlocks == 0, "swap for this hash is already initiated");
        require(participant != address(0), "invalid participant address");
        require(block.number < refundTimeInBlocks, "refundTimeInBlocks has already come");
        _;
    }

    modifier isRefundable(bytes32 hashedSecret) {
        require(block.number >= swaps[hashedSecret].refundTimeInBlocks);
        require(msg.sender == swaps[hashedSecret].initiator);
        _;
    }
    
    modifier isRedeemable(bytes32 hashedSecret, bytes32 secret) {
        require(msg.sender == swaps[hashedSecret].participant, "invalid msg.sender");
        require(block.number < swaps[hashedSecret].refundTimeInBlocks, "too late");
        require(sha256(abi.encodePacked(secret)) == hashedSecret, "invalid secret");
        _;
    }
    
    function initiate(uint refundTimeInBlocks, bytes32 hashedSecret, address participant, address contractAddress, uint256 value) public
        isNotInitiated(refundTimeInBlocks, hashedSecret, participant)
    {
        swaps[hashedSecret].refundTimeInBlocks = refundTimeInBlocks;
        swaps[hashedSecret].contractAddress = contractAddress;
        swaps[hashedSecret].participant = participant;
        swaps[hashedSecret].initiator = msg.sender;
        swaps[hashedSecret].value = value;

        IERC20(contractAddress).transferFrom(msg.sender, address(this), value);
    }
    
    function redeem(bytes32 secret, bytes32 hashedSecret) public
        isRedeemable(hashedSecret, secret)
    {
        Swap memory tmp = swaps[hashedSecret];
        delete swaps[hashedSecret];

        IERC20(tmp.contractAddress).safeTransfer(tmp.participant, tmp.value);
    }

    function refund(bytes32 hashedSecret) public
        isRefundable(hashedSecret) 
    {
        Swap memory tmp = swaps[hashedSecret];
        delete swaps[hashedSecret];

        IERC20(tmp.contractAddress).safeTransfer(tmp.initiator, tmp.value);
    }

    function getSwapDetails(bytes32 hashedSecret)
    public view returns (uint refundTimeInBlocks, address contractAddress, address initiator, address participant, uint256 value)
    {
        refundTimeInBlocks = swaps[hashedSecret].refundTimeInBlocks;
        contractAddress = swaps[hashedSecret].contractAddress;
        initiator = swaps[hashedSecret].initiator;
        participant = swaps[hashedSecret].participant;
        value = swaps[hashedSecret].value;
    }
}
