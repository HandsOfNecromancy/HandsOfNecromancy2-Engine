# Welcome to Hands of Necromancy II Engine!

Licensed under the GPL v3

If you want to try and get the game running on GZDoom, you're going to need to cherry-pick at least these several commits:

- https://github.com/HandsOfNecromancy/HandsOfNecromancy2-Engine/commit/bb33cf9b9f0feca8ae364e7a96ee7125c6d08899
  - This commit is necessary in order to keep the game running smoothly. It keeps the linked light lists from being updated every tic.
- https://github.com/HandsOfNecromancy/HandsOfNecromancy2-Engine/commit/0468fdade46e64018e4e2c94132ba78fb8616dbe
  - This commit is necessary in order to make the game look right. It allows rendering sprites under the ground.
- https://github.com/HandsOfNecromancy/HandsOfNecromancy2-Engine/commit/83ad8ed5d5784458fe7c86fa54a4b980774ef8a3
  - This commit supports the skyward projectiles trick by not unnecessarily moving light positions for non-interacting objects
- https://github.com/HandsOfNecromancy/HandsOfNecromancy2-Engine/commit/55ce9f02e9d3ffc37539feb128b438d1f2039f8d
  - This commit is necessary in order to do proper font displays for the actor label names
