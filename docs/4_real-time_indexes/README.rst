Chapter 4. Real-time indexes
============================

Real-time indexes (or RT indexes for brevity) are a new backend that
lets you insert, update, or delete documents (rows) on the fly. RT
indexes were added in version 1.10-beta. While querying of RT indexes is
possible using any of the SphinxAPI, SphinxQL, or SphinxSE, updating
them is only possible via SphinxQL at the moment. Full SphinxQL
reference is available in `Chapter 8, *SphinxQL
reference* <../8_sphinxql_reference/README.html>`__.
