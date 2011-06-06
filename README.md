Basic Evented Web Server
========================

This is a project I've wanted to do for quite a long time now. It's a web
server built around the single-thread event loop design. The C10k problem was
solved a long time ago (see: [nginx][1], [lighttpd][2], [Cherokee][3], etc.),
so I am not blazing new trail here but that isn't the point anyway. I am
a server geek and this stuff is interesting to me, so implementing it myself is
a fun project that I will learn a lot from.

Implementation
--------------
I like C/C++ and plan on using mostly C but with basic C++ features where that
makes sense. I don't want to go crazy with objects or templates or any of the
esoteric features of C++, just enough to be easier than straight C.

Since I work on a Mac mainly, the initial implementation of the event loop is
based on [Kqueue][4]. I hope to add a suitable Linux event interface as well,
probably [Epoll][5], but that will come later. I decided not to use a premade
event loop library like [libev][6] because that is one of the most interesting
parts of this project for me.

For HTTP parsing, I am using Ryan Dahl's [http-parser][7] library since it is small,
easy to use, and parsing HTTP is boring.

Roadmap
-------
Features yet to be implemented, listed in order of importance (to me):

 * static file serving
 * directory indexes
 * Epoll
 * module API (for things like mod_fcgi etc.)

This list is very short, and there will likely be things that pop up in between
implementing the things on the list so it's just a vague outline.


[1]: http://nginx.org/en/ "nginx (engine x) by Igor Sysoev"
[2]: http://www.lighttpd.net "lighttpd (lighty) 'fly light.'"
[3]: http://www.cherokee-project.net "Cherokee Web Server"
[4]: http://en.wikipedia.org/wiki/Kqueue "Wikipedia article on Kqueue"
[5]: http://en.wikipedia.org/wiki/Epoll "Wikipedia article on Epoll"
[6]: http://software.schmorp.de/pkg/libev.html "Homepage of libev"
[7]: https://github.com/ry/http-parser "http-parser on GitHub"