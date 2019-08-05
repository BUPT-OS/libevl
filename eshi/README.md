
== PURPOSE of libeshi

The EVL shim library mimics the behavior of the original EVL API based
on plain POSIX calls from the native *libc, which does not require the
EVL core to be enabled in the host kernel. It is useful when the
real-time guarantees delivered by the EVL core are not required for
quick prototyping or debugging application code.

== USAGE

To build against the EVL shim library, you need to update a couple of
settings from the Makefile building your application as follows (*):

CPPFLAGS/CFLAGS:
- Replace -I$(prefix)/include by -I$(prefix)/include/eshi

LDFLAGS:
- Replace -levl by -leshi

For instance:

$(CC) -o app app.c -I$(prefix)/include/eshi -L$(prefix)/lib -leshi -lpthread -lrt

(*) $(prefix) is the installation root of libevl (e.g. /usr/evl).

== LIMITATIONS

This API supports process-local services only. The resources/objects
it creates cannot be shared between processes.

The following calls are not supported:

evl_peek_flags
evl_peek_sem
evl_new_xbuf
evl_open_mutex
evl_open_event
evl_open_flags
evl_open_sem

== VARIATION(S)

evl_poll: POLLOUT is always set for evl_flags. Unlike with the native
libevl API, this condition cannot be monitored for detecting event
consumption by the receiver side, i.e. waiting for the value to be
cleared upon a successful call to evl_wait*_flags.
