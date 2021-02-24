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
