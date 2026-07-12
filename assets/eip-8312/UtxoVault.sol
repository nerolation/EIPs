// SPDX-License-Identifier: CC0-1.0
pragma solidity ^0.8.30;

/// Reference implementation of the UTXO vault deposit code.
/// The canonical runtime code is the geas program in utxo_vault.eas;
/// this contract mirrors its behavior. Storage slot 0 holds
/// `next_utxo_index`. All other vault state (openings roots, batch
/// roots, spent bits) is written by the protocol directly.
contract UtxoVault {
    uint256 private nextIndex; // slot 0

    event UtxoCreated(
        address indexed source,
        address indexed recipient,
        uint64 index,
        uint256 value
    );

    fallback() external payable {
        require(msg.data.length == 20 && msg.value > 0);
        address recipient = address(bytes20(msg.data));
        require(recipient != address(0));
        emit UtxoCreated(msg.sender, recipient, uint64(nextIndex++), msg.value);
    }
}
