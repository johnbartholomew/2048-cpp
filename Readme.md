Overview
--------

This is a C++ implementation of the '2048' game.
It is primarily inspired by this [HTML + Javascript version](https://github.com/gabrielecirulli/2048)
by Gabriele Cirulli:

Key controls:

* Escape -- exit the program
* Arrow keys -- move
* 'n' -- new game
* 'h' -- make one auto-move
* 'p' -- toggle auto-play
* 'z' -- undo
* 'x' -- redo

Build requirements
------------------

* A C++ compiler (the Makefile uses g++)
* A C compiler (the Makefile uses gcc)
* GLFW 3 from http://www.glfw.org/
* FreeType 2 from http://freetype.org/download.html
* The 'Clear Sans' font, from https://01.org/clear-sans (specifically, the Bold version)
* Some way of rasterising an SVG file to an image (the Makefile uses Inkscape)

To Do
-----

Like any project, this one has an effectively infinite to-do list. Starting with the most interesting:

* Better board scoring heuristics (*many* possibilities here!)
* Run the search in a background thread
* Iterative Deepening Depth First Search (with caching)
* Retaining cache state between moves during autoplay
* Game over message
* Game WIN message (with a button to continue playing after you hit it)
* Seed the RNG properly (but let the seed be controlled by a command line switch)
* Game scoring
* No-GUI autoplay mode for trialling variations to the AI

Beyond this point I will most likely have got bored and moved onto other projects:

* Code structure clean-up
* Actually multi-threaded search (ie, split a single search over multiple cores)
* Alternative basic search/AI algorithms (e.g., maximise *expected* board score rather than assuming antagonistic placement of new tiles?)
* A man-page
* On-screen help (showing keyboard controls)
* Run-time configuration (via command line or a config file) of board scoring heuristic, search algorithm, search depth/time, win condition
* High score table (offline/local only)
* Pack data (e.g., font) into the executable
* Autotools build
* Clean (schroot or similar) build script to produce distributable binaries
* Win32 build?
* Release!

Done
----

* Alpha-Beta pruned minimax
* Alpha-Beta pruned minimax with caching

Legal / Intellectual Property
-----------------------------

I did not invent this game. I copied the game idea, game rules, styling and
animation behaviour (inexactly) from https://github.com/gabrielecirulli/2048
I did not copy any of the code from that implementation, but I did copy some
stylistic things (e.g., colour values). Gabriele Cirulli's implementation is
released under the MIT licence.

I did not create fontstash.h or glfontstash.h. Licensing details are included
in comment blocks in the files. Font Stash is written by Mikko Mononen, and is
available at: https://github.com/memononen/fontstash

I *did* write the code in tiles2048.cpp. I am releasing it under the MIT licence:

Copyright © 2014 John Bartholomew

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
