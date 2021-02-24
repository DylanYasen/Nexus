Nexus


```
sqlite tuning log @2021-2-24
================with index===================
Execution finished without errors.
Result: 117 rows returned in 165ms
At line 1:
SELECT * FROM files WHERE name like "%loot%"


Execution finished without errors.
Result: 1 rows returned in 3ms
At line 1:
SELECT * FROM files WHERE name like "ActionLoot_01.png"


================no index===================
Execution finished without errors.
Result: 1 rows returned in 9ms
At line 1:
SELECT * FROM files WHERE name is "ActionLoot_02.png"


Execution finished without errors.
Result: 117 rows returned in 9ms
At line 1:
SELECT * FROM files WHERE name like "%loot%"




!!!! using index speeds up "equal select"  but slows down "like select"


todo:
[] query by tags
[] file scan paths UI
[] file scanning in background thread
[] lazy load resources
[] resource management. (unload after every switch? LRU?)
[] audio stops when switching tab. optional?
[] audio auto switch to newly selected file 
[] audio play/pause hotkey
[] audio visualizer
