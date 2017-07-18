.. raw:: html

   <div class="navheader">

8.8. DELETE syntax
`Prev <sphinxql-replace.html>`__ 
Chapter 8. SphinxQL reference
 `Next <sphinxql-set.html>`__

--------------

.. raw:: html

   </div>

.. raw:: html

   <div class="sect1">

.. raw:: html

   <div class="titlepage">

.. raw:: html

   <div>

.. raw:: html

   <div>

.. rubric:: 8.8. DELETE syntax
   :name: delete-syntax
   :class: title

.. raw:: html

   </div>

.. raw:: html

   </div>

.. raw:: html

   </div>

.. code:: programlisting

    DELETE FROM index WHERE where_condition

DELETE statement, introduced in version 1.10-beta, is only supported for
RT indexes and for distributed which contains only RT indexes as agents
It deletes existing rows (documents) from an existing index based on ID.

``index`` is the name of RT index from which the row should be deleted.

``where_condition`` has the same syntax as in the SELECT statement (see
`Section 8.1, “SELECT syntax” <sphinxql-select.html>`__ for details).

.. code:: programlisting

    mysql> select * from rt;
    +------+------+-------------+------+
    | id   | gid  | mva1        | mva2 |
    +------+------+-------------+------+
    |  100 | 1000 | 100,201     | 100  |
    |  101 | 1001 | 101,202     | 101  |
    |  102 | 1002 | 102,203     | 102  |
    |  103 | 1003 | 103,204     | 103  |
    |  104 | 1004 | 104,204,205 | 104  |
    |  105 | 1005 | 105,206     | 105  |
    |  106 | 1006 | 106,207     | 106  |
    |  107 | 1007 | 107,208     | 107  |
    +------+------+-------------+------+
    8 rows in set (0.00 sec)

    mysql> delete from rt where match ('dumy') and mva1>206;
    Query OK, 2 rows affected (0.00 sec)

    mysql> select * from rt;
    +------+------+-------------+------+
    | id   | gid  | mva1        | mva2 |
    +------+------+-------------+------+
    |  100 | 1000 | 100,201     | 100  |
    |  101 | 1001 | 101,202     | 101  |
    |  102 | 1002 | 102,203     | 102  |
    |  103 | 1003 | 103,204     | 103  |
    |  104 | 1004 | 104,204,205 | 104  |
    |  105 | 1005 | 105,206     | 105  |
    +------+------+-------------+------+
    6 rows in set (0.00 sec)

    mysql> delete from rt where id in (100,104,105);
    Query OK, 3 rows affected (0.01 sec)

    mysql> select * from rt;
    +------+------+---------+------+
    | id   | gid  | mva1    | mva2 |
    +------+------+---------+------+
    |  101 | 1001 | 101,202 | 101  |
    |  102 | 1002 | 102,203 | 102  |
    |  103 | 1003 | 103,204 | 103  |
    +------+------+---------+------+
    3 rows in set (0.00 sec)

    mysql> delete from rt where mva1 in (102,204);
    Query OK, 2 rows affected (0.01 sec)

    mysql> select * from rt;
    +------+------+---------+------+
    | id   | gid  | mva1    | mva2 |
    +------+------+---------+------+
    |  101 | 1001 | 101,202 | 101  |
    +------+------+---------+------+
    1 row in set (0.00 sec)

.. raw:: html

   </div>

.. raw:: html

   <div class="navfooter">

--------------

+-------------------------------------+------------------------------------+---------------------------------+
| `Prev <sphinxql-replace.html>`__    | `Up <sphinxql-reference.html>`__   |  `Next <sphinxql-set.html>`__   |
+-------------------------------------+------------------------------------+---------------------------------+
| 8.7. REPLACE syntax                 | `Home <index.html>`__              |  8.9. SET syntax                |
+-------------------------------------+------------------------------------+---------------------------------+

.. raw:: html

   </div>
