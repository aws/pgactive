CREATE SCHEMA bdr_toy;

SET search_path = bdr_toy;

CREATE TABLE bdr_toy_tbl(
    toy_col integer
);

INSERT INTO bdr_toy_tbl (toy_col) VALUES (1);
