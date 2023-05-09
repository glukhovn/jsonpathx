CREATE EXTENSION jsonpathx;

SELECT jsonpath_embed_vars('$.a + $a', '"aaa"');
SELECT jsonpath_embed_vars('$.a + $a', '{"b": "abc"}');
SELECT jsonpath_embed_vars('$.a + $a', '{"a": "abc"}');
SELECT jsonpath_embed_vars('$.a + $a.double()', '{"a": "abc"}');
SELECT jsonpath_embed_vars('$.a + $a.x.double()', '{"a": {"x": -12.34}}');
SELECT jsonpath_embed_vars('$[*] ? (@ > $min && @ <= $max)', '{"min": -1.23, "max": 5.0}');
SELECT jsonpath_embed_vars(jsonpath_embed_vars('$[*] ? (@ > $min && @ <= $max)', '{"min": -1.23}'), '{"max": 5.0}');

CREATE TABLE testjsonb AS SELECT jsonb_build_object ('age', i % 100) j FROM generate_series(1, 10000) i;

CREATE INDEX jidx ON testjsonb USING gin (j jsonb_path_ops);

EXPLAIN (COSTS OFF)
SELECT * FROM testjsonb WHERE jsonb_path_match(j, '$.age == 25');
EXPLAIN (COSTS OFF)
SELECT * FROM testjsonb WHERE jsonb_path_match(j, '$.age == 25', silent => true);
EXPLAIN (COSTS OFF)
SELECT * FROM testjsonb WHERE jsonb_path_match(j, '$.age == 25', vars => '{"age": 34 }', silent => true);
EXPLAIN (COSTS OFF)
SELECT * FROM testjsonb WHERE jsonb_path_match(j, '$.age == $age', vars => '{"age": 25 }', silent => true);
EXPLAIN (COSTS OFF)
SELECT * FROM testjsonb WHERE jsonb_path_match(j, '$.age == $age', vars => '{"age": [25] }', silent => true);
EXPLAIN (COSTS OFF)
SELECT * FROM testjsonb WHERE jsonb_path_match(j, '$.age == $x || $.age == $y', vars => '{"x": 25, "y": 34}', silent => true);
EXPLAIN (COSTS OFF)
SELECT * FROM testjsonb t1, testjsonb t2 WHERE jsonb_path_match(t1.j, '$.age == $age', vars => t2.j, silent => true);

EXPLAIN (COSTS OFF)
SELECT * FROM testjsonb WHERE jsonb_path_exists(j, '$ ? (@.age == 25)');
EXPLAIN (COSTS OFF)
SELECT * FROM testjsonb WHERE jsonb_path_exists(j, '$ ? (@.age == 25)', silent => true);
EXPLAIN (COSTS OFF)
SELECT * FROM testjsonb WHERE jsonb_path_exists(j, '$ ? (@.age == 25)', vars => '{"age": 34 }', silent => true);
EXPLAIN (COSTS OFF)
SELECT * FROM testjsonb WHERE jsonb_path_exists(j, '$ ? (@.age == $age)', vars => '{"age": 25 }', silent => true);
EXPLAIN (COSTS OFF)
SELECT * FROM testjsonb WHERE jsonb_path_exists(j, '$ ? (@.age == $age)', vars => '{"age": [25] }', silent => true);
EXPLAIN (COSTS OFF)
SELECT * FROM testjsonb WHERE jsonb_path_exists(j, '$ ? (@.age == $x || $.age == $y)', vars => '{"x": 25, "y": 34}', silent => true);
EXPLAIN (COSTS OFF)
SELECT * FROM testjsonb t1, testjsonb t2 WHERE jsonb_path_exists(t1.j, '$ ? (@.age == $age)', vars => t2.j, silent => true);

DROP EXTENSION jsonpathx;
