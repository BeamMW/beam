
![alt text](https://s3.eu-central-1.amazonaws.com/website-storage.beam.mw/media/homepage/scc/scc-1.jpg "Beam Logo")

### Welcome to Beam


BEAM is a next generation scalable, confidential cryptocurrency based on an elegant and innovative [Mimblewimble protocol](https://docs.beam.mw/Mimblewimble.pdf).

### ANNOUNCEMENTS

Beam blockchian will **hard fork** on height 321,321 (approximately August 15th). Make sure to upgrade your wallets, nodes and mining software to the latest version. Hard Fork will have the following breaking changes:

* PoW algorithm from Beam Hash I to Beam Hash II. More details on Beam Hash II can be found [here](https://docs.beam.mw/BeamHashII.pdf)
* SBBS PoW (automatically performed by newer wallets) will become mandatory
* A minimum fee of ( 10 * Number of kernels + 10 * Number of outputs ) will be enforced by the nodes. The default fee value in the wallet will be changed to 100 Groth
* Support for Relative Time Locks




[Download latest version](http://beam.mw/downloads)
**Clear Cathode 3.0 with Hard Fork Support**



### JOIN OUR COMMUNITIES ON TELEGRAM

[English](https://t.me/BeamPrivacy) | [Русский](https://t.me/Beam_RU) | [中文](https://t.me/beamchina) | [日本語](https://t.me/beamjp)

### FOLLOW BEAM 

[twitter](https://twitter.com/beamprivacy) | [medium](https://medium.com/beam-mw) | [reddit](https://www.reddit.com/r/beamprivacy/) | [gitter](https://gitter.im/beamprivacy/Lobby) | [bitcointalk](https://bitcointalk.org/index.php?topic=5052151.0) | [youtube](https://www.youtube.com/channel/UCddqBnfSPWibf4f8OnEJm_w?)


### GET STARTED


Get Beam binaries here: http://beam.mw/downloads

If you build from source please use 'mainnet' branch\

Peers:

eu-nodes.mainnet.beam.mw:8100

us-nodes.mainnet.beam.mw:8100

ap-nodes.mainnet.beam.mw:8100

Latest documentation is here: https://documentation.beam.mw

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

[Read our position paper](https://docs.beam.mw/BEAM_Position_Paper_v0.2.2.pdf)

### Roadmap

See Beam roadmap for 2019 on https://beam.mw

### Current status

#### Mainnet(January 3rd 2019)

- Fully functional wallet with key generator and storage supporting secure and confidential online transactions.
- Full node with both transaction and block validation and full UTXO state management.
- Equihash miner with periodic mining difficulty adjustment.
- Batch Bulletproofs, the efficient non-interactive zero knowledge range proofs now in batch mode
- Graphical Wallet Application for Linux, Mac and Windows platforms
- Offline transactions using Secure BBS system
- ChainWork - sublinear blockchain validation, based on FlyClient idea by Loi Luu, Benedikt Bünz, Mahdi Zamani
- Compact history using cut through

#### Agile Atom (February 2019)

- Payment and Exchange APIs
- Mining Pool APIs
- Lightning Network position paper

#### Bright Boson 2.0 (March 2019)
- Payment proof
- Ultra fast sync
- Android Mobile wallet

See [How to build](https://github.com/BeamMW/beam/wiki/How-to-build)

# Build status
[![Build Status](https://travis-ci.org/BeamMW/beam.svg?branch=master)](https://travis-ci.org/BeamMW/beam)
[![Build status](https://ci.appveyor.com/api/projects/status/0j424l1h61gwqddm/branch/master?svg=true)](https://ci.appveyor.com/project/beam-mw/beam/branch/master)

