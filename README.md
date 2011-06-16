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
I like C/C++ and plan on using mostly C but with basic C++ features where they
makes sense. I don't want to go crazy with objects or templates or any of the
esoteric features of C++, just enough to be easier than straight C.

Since I work on a Mac mainly, the initial implementation of the event loop is
based on [Kqueue][4]. I hope to add a suitable Linux event interface as well,
probably [Epoll][5], but that will come later. I decided not to use a premade
event loop library like [libev][6] because that is one of the most interesting
parts of this project for me.

For HTTP request parsing, I am using Ryan Dahl's [http-parser][7] library since it is small,
easy to use, and parsing HTTP is boring.

Roadmap
-------
Features yet to be implemented, listed in order of importance (to me):

 * static file serving

   This includes all the basic web server machinery - connection handling, header processing, response generation, etc.
 
 * directory indexes

   This might seem like an obvious feature, but I think it will prove to be more complex than it seems, traversing directories and generating an HTML page representing their structure is a bit of work; I am also thinking that this may fit into a module once that API is in place, we'll see...

 * virtual hosts

   Being able to host multiple sites on one server is an essential and basic feature these days. Should be a pretty easy deal too, basically just need to correlate each Host with its own docroot.

 * configuration file

   At this point, there are enough features to warrant a method for configuring the server without requiring recompilation. I could have included this feature earlier, but I think it will be easier to implement once a base level of functionality is in place. Not sure what format configuration files will take at this point, might even use something like [Lua][8] if it's easy enough since that would add a great deal of power...

 * Epoll

   I am not interested in supporting every platform out there (Windows, older Linux, other *nix's are all well served by other software) so I think between Kqueue (FreeBSD & OS X) and Epoll (Linux 2.6+) I will be satisfied with multiplatform support. If others would like to contribute support for other systems, it may be easier once I have two implementations in place and I would be happy to accept contributions.

 * module API (for things like mod_fcgi etc.)

   Having never written a plugin API before, this part will definitely be tricky. I'm not sure if I'll want to (or be able to...) support loadable modules or if I'll stick to the model that requires modules to be compiled in a la nginx. This will require lots of research and will most definitely be modeled after other servers' module systems.

This list is very short, and there will likely be things that pop up in between
implementing the things on the list so it's just a vague outline for now.


[1]: http://nginx.org/en/ "nginx (engine x) by Igor Sysoev"
[2]: http://www.lighttpd.net "lighttpd (lighty) 'fly light.'"
[3]: http://www.cherokee-project.net "Cherokee Web Server"
[4]: http://en.wikipedia.org/wiki/Kqueue "Wikipedia article on Kqueue"
[5]: http://en.wikipedia.org/wiki/Epoll "Wikipedia article on Epoll"
[6]: http://software.schmorp.de/pkg/libev.html "Homepage of libev"
[7]: https://github.com/ry/http-parser "http-parser on GitHub"