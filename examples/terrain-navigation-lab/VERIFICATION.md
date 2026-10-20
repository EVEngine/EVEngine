# Recovery and verification

This is a reconstruction after the old workspace was reset. Original commits
a09bb50d8 and 73b585f5d were not found in the current object store or on GitHub.
Original raw logs and preview could not be recovered. No old measurement is
presented as evidence for this code.

The intended checks are:
1. A* and flow-field reachability on continuous terrain; 1 km CPU navigation case.
2. Physical uphill restriction, downhill/lateral movement and policy override.
3. A 120-second rendered scene run with diagnostic and memory monitoring.

Current runs and their exact limitations will be recorded below. A finite scene
does not establish infinite-world streaming or production reliability. Earlier
conversation records mention eight terrain-suite assertion mismatches; their raw
logs are unavailable, so their current status is unknown.
