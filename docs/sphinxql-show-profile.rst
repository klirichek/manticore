.. raw:: html

   <div class="navheader">

8.34. SHOW PROFILE syntax
`Prev <sphinxql-show-agent-status.html>`__ 
Chapter 8. SphinxQL reference
 `Next <sphinxql-show-index-status.html>`__

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

.. rubric:: 8.34. SHOW PROFILE syntax
   :name: show-profile-syntax
   :class: title

.. raw:: html

   </div>

.. raw:: html

   </div>

.. raw:: html

   </div>

.. code:: programlisting

    SHOW PROFILE

SHOW PROFILE statement, added in version 2.1.1-beta, shows a detailed
execution profile of the previous SQL statement executed in the current
SphinxQL session. Also, profiling must be enabled in the current session
**before** running the statement to be instrumented. That can be done
with a ``SET profiling=1`` statement. By default, profiling is disabled
to avoid potential performance implications, and therefore the profile
will be empty.

Here’s a complete instrumentation example:

.. code:: programlisting

    mysql> SET profiling=1;
    Query OK, 0 rows affected (0.00 sec)

    mysql> SELECT id FROM lj WHERE MATCH('the test') LIMIT 1;
    +--------+
    | id     |
    +--------+
    | 946418 |
    +--------+
    1 row in set (0.05 sec)

    mysql> SHOW PROFILE;
    +--------------+----------+----------+
    | Status       | Duration | Switches |
    +--------------+----------+----------+
    | unknown      | 0.000610 | 6        |
    | net_read     | 0.000007 | 1        |
    | dist_connect | 0.000036 | 1        |
    | sql_parse    | 0.000048 | 1        |
    | dict_setup   | 0.000001 | 1        |
    | parse        | 0.000023 | 1        |
    | transforms   | 0.000002 | 1        |
    | init         | 0.000401 | 3        |
    | open         | 0.000104 | 1        |
    | read_docs    | 0.001570 | 71       |
    | read_hits    | 0.003936 | 222      |
    | get_docs     | 0.029837 | 1347     |
    | get_hits     | 0.000548 | 1433     |
    | filter       | 0.000619 | 1274     |
    | rank         | 0.009892 | 2909     |
    | sort         | 0.001562 | 52       |
    | finalize     | 0.000250 | 1        |
    | dist_wait    | 0.000000 | 1        |
    | aggregate    | 0.000145 | 1        |
    | net_write    | 0.000031 | 1        |
    +--------------+----------+----------+
    20 rows in set (0.00 sec)

Status column briefly describes where exactly (in which state) was the
time spent. Duration column shows the wall clock time, in seconds.
Switches column displays the number of times query engine changed to the
given state. Those are just logical engine state switches and **not**
any OS level context switches nor function calls (even though some of
the sections can actually map to function calls) and they do **not**
have any direct effect on the performance. In a sense, number of
switches is just a number of times when the respective instrumentation
point was hit.

States in the profile are returned in a prerecorded order that roughly
maps (but is **not** identical) to the actual query order.

A list of states may (and will) vary over time, as we refine the states.
Here’s a brief description of the currently profiled states.

.. raw:: html

   <div class="itemizedlist">

-  **unknown**, generic catch-all state. Accounts for both
   not-yet-instrumented code, or just small miscellaneous tasks that do
   not really belong in any other state, but are too small to deserve
   their own state.
-  **net\_read**, reading the query from the network (that is, the
   application).
-  **io**, generic file IO time.
-  **dist\_connect**, connecting to remote agents in the distributed
   index case.
-  **sql\_parse**, parsing the SphinxQL syntax.
-  **dict\_setup**, dictionary and tokenizer setup.
-  **parse**, parsing the full-text query syntax.
-  **transforms**, full-text query transformations (wildcard and other
   expansions, simplification, etc).
-  **init**, initializing the query evaluation.
-  **open**, opening the index files.
-  **read\_docs**, IO time spent reading document lists.
-  **read\_hits**, IO time spent reading keyword positions.
-  **get\_docs**, computing the matching documents.
-  **get\_hits**, computing the matching positions.
-  **filter**, filtering the full-text matches.
-  **rank**, computing the relevance rank.
-  **sort**, sorting the matches.
-  **finalize**, finalizing the per-index search result set (last stage
   expressions, etc).
-  **dist\_wait**, waiting for the remote results from the agents in the
   distributed index case.
-  **aggregate**, aggregating multiple result sets.
-  **net\_write**, writing the result set to the network.

.. raw:: html

   </div>

.. raw:: html

   </div>

.. raw:: html

   <div class="navfooter">

--------------

+-----------------------------------------------+------------------------------------+-----------------------------------------------+
| `Prev <sphinxql-show-agent-status.html>`__    | `Up <sphinxql-reference.html>`__   |  `Next <sphinxql-show-index-status.html>`__   |
+-----------------------------------------------+------------------------------------+-----------------------------------------------+
| 8.33. SHOW AGENT STATUS                       | `Home <index.html>`__              |  8.35. SHOW INDEX STATUS syntax               |
+-----------------------------------------------+------------------------------------+-----------------------------------------------+

.. raw:: html

   </div>
