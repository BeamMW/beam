
![alt text](https://forum.beam-mw.com/uploads/beam_mw/original/1X/261e2a2eba2b6c8aadae678673f9e8e09a78f5cf.png "Beam Logo")

BEAM is a next generation scalable, confidential cryptocurrency based on an elegant and innovative [Mimblewimble protocol](https://docs.beam.mw/Mimblewimble.pdf).

[twitter](https://twitter.com/beamprivacy) | [medium](https://medium.com/beam-mw) | [reddit](https://www.reddit.com/r/beamprivacy/) | [beam forum](http://forum.beam-mw.com) | [gitter](https://gitter.im/beamprivacy/Lobby) | [telegram](https://t.me/BeamPrivacy) | [bitcointalk](https://bitcointalk.org/index.php?topic=5052151.0) | [youtube](https://www.youtube.com/channel/UCddqBnfSPWibf4f8OnEJm_w?)


[Read our position paper](https://docs.beam.mw/BEAM_Position_Paper_v0.2.2.pdf)


**MAINNET IS LAUNCHED!** 

CRITICAL VULNERABILITY IN BEAM WALLET 

DO NOT USE VERSIONS YOU HAVE BUILT FROM SOURCE ON MAINNET UNTIL FURTHER NOTICE

9.1.2019 20:20 GMT 

Critical Vulnerability was found in Beam Wallet today.

Vulnerability was discovered by Beam Dev Team and not reported anywhere else.

Vulnerability affects all previously released Beam Wallets both Dekstop and CLI.

DO NOT DELETE THE DATABASE or any other wallet data.

The vulnerability DOES NOT affect wallet data, secret keys or passwords

All Beam users are REQUIRED to follow the procedure below IMMEDIATELY!!!


1. Stop your currently running Beam Wallets immediately

2. Uninstall or delete your Beam Wallet application and executables from all machines. 

DO NOT DELETE THE DATABASE or any other wallet data

3. Make sure the application was deleted. Check the documentation for the location of Wallet application files (https://beam-docs.readthedocs.io/en/latest/rtd_pages/user_files_and_locations.html)

4. Download the Beam Wallet again from the website only ( https://www.beam.mw/downloads ) 
 
It will have THE SAME version numbers as previously published archives
Make sure the SHA256 of the archive matches with the one published on the website.

5. Install the new application



Details for the vulnerability and the CVE will be published within a week to avoid exploits.



============================================================================


https://beam.mw/downloads

If you build from source please use 'mainnet' branch

Peers:

eu-node01.mainnet.beam.mw:8100

eu-node02.mainnet.beam.mw:8100

eu-node03.mainnet.beam.mw:8100

eu-node04.mainnet.beam.mw:8100

us-node01.mainnet.beam.mw:8100

us-node02.mainnet.beam.mw:8100

us-node03.mainnet.beam.mw:8100

us-node04.mainnet.beam.mw:8100

ap-node01.mainnet.beam.mw:8100

ap-node02.mainnet.beam.mw:8100

ap-node03.mainnet.beam.mw:8100

ap-node04.mainnet.beam.mw:8100

Latest docs are here: https://beam-docs.readthedocs.io/en/latest/index.html

Things that make BEAM special include:

* Users have complete control over privacy - a user decides which information will be available and to which parties, having complete control over his personal data in accordance to his will and applicable laws.
* Confidentiality without penalty - in BEAM confidential transactions do not cause bloating of the blockchain, avoiding excessive computational overhead or penalty on performance or scalability while completely concealing the transaction value.
* No trusted setup required
* Blocks are mined using Equihash Proof-of-Work algorithm.
* Limited emission using periodic halving.
* No addresses are stored in the blockchain - no information whatsoever about either the sender or the receiver of a transaction is stored in the blockchain.
* Superior scalability through compact blockchain size - using the “cut-through” feature of
Mimblewimble makes the BEAM blockchain orders of magnitude smaller than any other
blockchain implementation.
* BEAM supports many transaction types such as escrow transactions, time locked
transactions, atomic swaps and more.
* No premine. No ICO. Backed by a treasury, emitted from every block during the first five
years.
* Implemented from scratch in C++.

# Roadmap

- March 2018     : Project started
- June 2018      : Internal POC featuring fully functional node and CLI wallet
- September 2018 : Testnet 1 and Desktop Wallet App (Windows, Mac, Linux)
- December 2018  : Mainnet launch

# Current status

- Fully functional wallet with key generator and storage supporting secure and confidential online transactions.
- Full node with both transaction and block validation and full UTXO state management.
- Equihash miner with periodic mining difficulty adjustment.
- Batch Bulletproofs, the efficient non-interactive zero knowledge range proofs now in batch mode
- Graphical Wallet Application for Linux, Mac and Windows platforms
- Offline transactions using Secure BBS system
- ChainWork - sublinear blockchain validation, based on FlyClient idea by Loi Luu, Benedikt Bünz, Mahdi Zamani
- Compact history using cut through

See [How to build](https://github.com/BeamMW/beam/wiki/How-to-build)

# Build status
[![Build Status](https://travis-ci.org/BeamMW/beam.svg?branch=master)](https://travis-ci.org/BeamMW/beam)
[![Build status](https://ci.appveyor.com/api/projects/status/0j424l1h61gwqddm/branch/master?svg=true)](https://ci.appveyor.com/project/beam-mw/beam/branch/master)

