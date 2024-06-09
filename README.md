# Welcome to Hands of Necromancy Engine!

Licensed under the GPL v3

If you want to try and get the game running on GZDoom, you're going to need to cherry-pick at least these two commits:

- https://github.com/HandsOfNecromancy/HandsOfNecromancy-Engine/commit/bb33cf9b9f0feca8ae364e7a96ee7125c6d08899
  - This commit is necessary in order to keep the game running smoothly. It keeps the linked light lists from being updated every tic.
- https://github.com/HandsOfNecromancy/HandsOfNecromancy-Engine/commit/0468fdade46e64018e4e2c94132ba78fb8616dbe
  - This commit is necessary in order to make the game look right. It allows rendering sprites under the ground.
