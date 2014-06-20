uwsgi-netlink
=============

uWSGI plugin exposing netlink features

installation
============

the plugin is 2.x friendly:

```sh
uwsgi --build-plugin https://github.com/unbit/uwsgi-netlink
```

usage
=====

currently the plugin only adds listen queue monitoring for unix sockets. The feature is automatically enabled on loading (and obviously, if UNIX sockets are bound)
