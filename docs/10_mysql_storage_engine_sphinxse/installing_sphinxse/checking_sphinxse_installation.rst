Checking SphinxSE installation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To check whether SphinxSE has been successfully compiled into MySQL,
launch newly built servers, run mysql client and issue ``SHOW ENGINES``
query. You should see a list of all available engines. Sphinx should be
present and “Support” column should contain “YES”:

::


    mysql> show engines;
    +------------+----------+-------------------------------------------------------------+
    | Engine     | Support  | Comment                                                     |
    +------------+----------+-------------------------------------------------------------+
    | MyISAM     | DEFAULT  | Default engine as of MySQL 3.23 with great performance      |
      ...
    | SPHINX     | YES      | Sphinx storage engine                                       |
      ...
    +------------+----------+-------------------------------------------------------------+
    13 rows in set (0.00 sec)

