# ReadMe

Radar is a ripple-protocol-based financial network to promote and develop the fastest and least expensive payment settlements systems on a global scale. It enables payments in all types of currency as easy as sending email. 

As the core component of the radar network, the underlying Radard is a series of servers that run peer to peer software. Anyone can freely set up a Radard server to being accepting or receiving payments. In addition to VRP, Radar network introduces a second native currency 'VBC' which is designed to promote the spread and adoption of the Radar network through a reward program. The reward program will be scheduled daily to distribute the pre-defined currency fairly to every eligible user. Using Radard is free and the Radar network is owned by every participant.

This is the repository for Radar's ``radard``, reference P2P server.

### Repository Contents
#### ./bin
Scripts and data files for Radard integrators.

#### ./build
Intermediate and final build outputs.

#### ./Builds
Platform or IDE-specific project files.

#### ./doc
Documentation and example configuration files.

#### ./src
Source code directory. Some of the directories contained here are external repositories inlined via git-subtree, see the corresponding README for more details.

### Transaction Fees
  1. Activating the wallet（Creating an account）：It requires a transfer to the newly created account from an existing account. Both owners need to pay 0.01 VRP plus transaction fee for the transfer（see Point 2 for details).
  2. Transaction fee for a transfer
     1. A transfer of VBC or VRP: the sender needs to pay a bill which is 1/1000 of the transfer amount in VRP as the transaction fee, and the minimum transaction fee is 0.001VRP.
     2. Transfer of other currencies: a fixed 0.001 VRP will be applied to each transaction
     3. Other types of transaction, e.g. Offer: a fixed 0.001 VRP will be applied to each transaction
  3. There is no minimum balance required for a wallet, i.e. the entire balance can be transferred out from an activated account.

For more information, see https://wiki.radarlab.org/transaction_fee.

### Build instructions:
  * Coming soon...

### Setup instructions:
  * Coming soon...

### License
Radar is open source and permissively licensed under the ISC license. See the LICENSE file for more details.

If you are a financial institution, please contact info@radr.biz for enterprise licensing. If you are not a financial institution and you offer public service, you must list on your website and terms of service that your service is an Unofficial Radr Gateway.
