.. raw:: html

   <div class="navheader">

9.2.1. SetLimits
`Prev <api-funcgroup-general-query-settings.html>`__ 
9.2. General query settings
 `Next <api-func-setmaxquerytime.html>`__

--------------

.. raw:: html

   </div>

.. raw:: html

   <div class="sect2">

.. raw:: html

   <div class="titlepage">

.. raw:: html

   <div>

.. raw:: html

   <div>

.. rubric:: 9.2.1. SetLimits
   :name: setlimits
   :class: title

.. raw:: html

   </div>

.. raw:: html

   </div>

.. raw:: html

   </div>

**Prototype:** function SetLimits ( $offset, $limit, $max\_matches=1000,
$cutoff=0 )

Sets offset into server-side result set (``$offset``) and amount of
matches to return to client starting from that offset (``$limit``). Can
additionally control maximum server-side result set size for current
query (``$max_matches``) and the threshold amount of matches to stop
searching at (``$cutoff``). All parameters must be non-negative
integers.

First two parameters to SetLimits() are identical in behavior to MySQL
LIMIT clause. They instruct ``searchd`` to return at most ``$limit``
matches starting from match number ``$offset``. The default offset and
limit settings are 0 and 20, that is, to return first 20 matches.

``max_matches`` setting controls how much matches ``searchd`` will keep
in RAM while searching. **All** matching documents will be normally
processed, ranked, filtered, and sorted even if ``max_matches`` is set
to 1. But only best N documents are stored in memory at any given moment
for performance and RAM usage reasons, and this setting controls that N.
Note that there are **two** places where ``max_matches`` limit is
enforced. Per-query limit is controlled by this API call, but there also
is per-server limit controlled by ``max_matches`` setting in the config
file. To prevent RAM usage abuse, server will not allow to set per-query
limit higher than the per-server limit.

You can’t retrieve more than ``max_matches`` matches to the client
application. The default limit is set to 1000. Normally, you must not
have to go over this limit. One thousand records is enough to present to
the end user. And if you’re thinking about pulling the results to
application for further sorting or filtering, that would be **much**
more efficient if performed on Sphinx side.

``$cutoff`` setting is intended for advanced performance control. It
tells ``searchd`` to forcibly stop search query once ``$cutoff`` matches
had been found and processed.

.. raw:: html

   </div>

.. raw:: html

   <div class="navfooter">

--------------

+---------------------------------------------------------+------------------------------------------------------+---------------------------------------------+
| `Prev <api-funcgroup-general-query-settings.html>`__    | `Up <api-funcgroup-general-query-settings.html>`__   |  `Next <api-func-setmaxquerytime.html>`__   |
+---------------------------------------------------------+------------------------------------------------------+---------------------------------------------+
| 9.2. General query settings                             | `Home <index.html>`__                                |  9.2.2. SetMaxQueryTime                     |
+---------------------------------------------------------+------------------------------------------------------+---------------------------------------------+

.. raw:: html

   </div>
