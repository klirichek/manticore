.. raw:: html

   <div class="navheader">

12.2.37. inplace\_enable
`Prev <conf-preopen.html>`__ 
12.2. Index configuration options
 `Next <conf-inplace-hit-gap.html>`__

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

.. rubric:: 12.2.37. inplace\_enable
   :name: inplace_enable
   :class: title

.. raw:: html

   </div>

.. raw:: html

   </div>

.. raw:: html

   </div>

Whether to enable in-place index inversion. Optional, default is 0 (use
separate temporary files). Introduced in version 0.9.9-rc1.

``inplace_enable`` greatly reduces indexing disk footprint, at a cost of
slightly slower indexing (it uses around 2x less disk, but yields around
90-95% the original performance).

Indexing involves two major phases. The first phase collects, processes,
and partially sorts documents by keyword, and writes the intermediate
result to temporary files (.tmp\*). The second phase fully sorts the
documents, and creates the final index files. Thus, rebuilding a
production index on the fly involves around 3x peak disk footprint: 1st
copy for the intermediate temporary files, 2nd copy for newly
constructed copy, and 3rd copy for the old index that will be serving
production queries in the meantime. (Intermediate data is comparable in
size to the final index.) That might be too much disk footprint for big
data collections, and ``inplace_enable`` allows to reduce it. When
enabled, it reuses the temporary files, outputs the final data back to
them, and renames them on completion. However, this might require
additional temporary data chunk relocation, which is where the
performance impact comes from.

This directive does not affect ``searchd`` in any way, it only affects
``indexer``.

.. rubric:: Example:
   :name: example

.. code:: programlisting

    inplace_enable = 1

.. raw:: html

   </div>

.. raw:: html

   <div class="navfooter">

--------------

+---------------------------------+---------------------------------+-----------------------------------------+
| `Prev <conf-preopen.html>`__    | `Up <confgroup-index.html>`__   |  `Next <conf-inplace-hit-gap.html>`__   |
+---------------------------------+---------------------------------+-----------------------------------------+
| 12.2.36. preopen                | `Home <index.html>`__           |  12.2.38. inplace\_hit\_gap             |
+---------------------------------+---------------------------------+-----------------------------------------+

.. raw:: html

   </div>
