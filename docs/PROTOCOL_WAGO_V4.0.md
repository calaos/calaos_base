# Calaos UDP protocol — what 4.0 changes

Port 4646, text frames, one reply per request, **positional** attribution on the server side
(`WagoMap::udpRequest_cb`): whatever arrives is the reply to the command at the head of the queue.

## Announced version

`WAGO_GET_VERSION` replies `WAGO_GET_VERSION <H>.<L> <model>`. The programs in this repository announce
**4.0** (`Config.CALAOS_VERSION_H` / `_L`).

`calaos_server` makes protocol choices based on this value — `plcDaliGetCarriesGroup()`
compares it against the `DALI_GROUP_MAJOR.DALI_GROUP_MINOR` threshold, which is 3.0. **Bump the version as
soon as the contract below changes**, otherwise the server has no way of knowing what to expect.

The `<model>` field has been correct since 4.0 (each target announces its own). It was not
before: all seven 3.0 programs replied `750-849`. Do not derive anything from it on the server side.

## The `address` field carries three meanings

The same value means, depending on context:

| range | meaning |
|---|---|
| `0..DMX_ADDR_BASE` | DALI short address, or DALI group number if the group flag is 1 |
| `DMX_ADDR_BASE+1 .. DMX_ADDR_BASE+DMX_CHANNELS` | DMX channel, numbered `address - DMX_ADDR_BASE` |

`DMX_ADDR_BASE` is 100 and is an **exclusive** bound: `abDMX_Values` is an `ARRAY[1..255]`, so
the first DMX channel is address 101. Address 100 is not a DMX address.

## Parameter order — the asymmetry is intentional

- `WAGO_DALI_SET <line> <group flag> <address> <level> <fade>`
- `WAGO_DALI_GET <line> <address> [<group flag>]`

The flag comes **before** the address on write, **after** it on read. A program older than
3.0 reads the address from that same second field: the flag was therefore added in third position
so as not to move the address. `calaos_server` only sends this third field if the announced
version is ≥ 3.0.

## Error replies — new in 4.0

`WAGO_DALI_GET ERR <reason>`

A program announcing **4.0 or later** may reply in this form to a read it cannot
serve. Reason emitted today: `GROUP_UNSUPPORTED` — reading back a group on a standard DALI
bus, impossible by construction (a DALI backward frame comes from a single device; a request
addressed to a group would cause a collision; the 753-647 path, on the other hand, does reply because it keeps
its own image on the program side).

Two constraints shaped this form, and they must be understood before changing it:

- **The verb is kept.** `udpReplyMisattributedToDaliGet` discards any reply whose verb
  starts with `WAGO_` without being `WAGO_DALI_GET` while a DALI read is in flight. A
  `WAGO_DALI_GET_ERROR` would be lost and the caller would only see the 2 s timeout.
- **`ERR` is not numeric.** The server reads any first field other than the string `"0"`
  as "on". A sentinel taken from the value domain — `-1`, `0` — is therefore read
  as a state. No reply must ever encode "I don't know" within the value domain.

### What `calaos_server` needs to implement

Until this is done, `ERR` is read as "on", exactly as `-1` was.

1. In the DALI read callback, test the first field **before** any numeric
   conversion: if it is `ERR`, report a failure — the same thing as a `status = false` on
   timeout — and log the reason.
2. Gate this test on the announced version if you want to stay strict: only a program ≥ 4.0 emits
   this form.
3. Do not add a dedicated verb on the PLC side without first lifting the filter in
   `udpReplyMisattributedToDaliGet`.

## Request serialization

`WagoMap::UDPCommand_cb` sends nothing while the command at the head of the queue is `inProgress`, and
`udpRequest_cb` only pops on reply: **only one command is in flight at a time**. Two
consequences for the PLC:

- the races described in [T-8](done/T-8.md) cannot come from `calaos_server`; the latches
  set on the PLC side are a defense against another UDP sender, not against it;
- a read with no reply blocks the queue for **2 s**, the length of the timeout. Replying with an error is therefore
  strictly cheaper than staying silent.
