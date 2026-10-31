# Link play

Trades and battles between two devices.

## Link play (trades and battles between two devices)

*Settings > Link play* links two devices running the app the way two DSs link up: one
taps *Host*, the other picks it from the list of devices hosting on the same Wi-Fi (or
types its address, e.g. on another network through a VPN such as Tailscale). Then in the
game, go upstairs in any Pokemon Center: the middle counter's **Union Room** (talk to the
other player there to trade, battle or chat) and the right-hand counter's **Colosseum**
(battles) work as on the DS.

The DS's local wireless is relayed over the network by libntr (enet, UDP port 2009; hosts
are found through a broadcast on port 2010). While linked the overworld runs at the DS's
own 30 Hz logic, which both sides' wireless code counts in. The command line's
`--net-host` / `--net-client` still work on desktop.

Tested between two desktop instances: in the Union Room, showing trainer cards, a trade
(both games saved the swapped Pokemon) and a battle all went through. It hasn't been tried
on two Android devices yet, so treat it as a first version there. Both devices need the
same game (a Renegade Platinum save won't trade with a plain Platinum one), and leaving the
app while linked drops the link, as closing a DS would.
