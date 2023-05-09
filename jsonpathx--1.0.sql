/* contrib/jsonpathx/jsonpathx--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION jsonpathx" to load this file. \quit

CREATE FUNCTION jsonpath_embed_vars(jsp jsonpath, vars jsonb) RETURNS jsonpath
LANGUAGE C STRICT IMMUTABLE
AS 'MODULE_PATHNAME';

COMMENT ON FUNCTION jsonpath_embed_vars IS 'embed variable values inside jsonpath';

CREATE FUNCTION jsonb_path_exists_support(internal) RETURNS internal
LANGUAGE C
AS 'MODULE_PATHNAME';

COMMENT ON FUNCTION jsonb_path_exists_support IS 'planner support for jsonb_path_exists';

CREATE FUNCTION jsonb_path_match_support(internal) RETURNS internal
LANGUAGE C
AS 'MODULE_PATHNAME';

COMMENT ON FUNCTION jsonb_path_match_support IS 'planner support for jsonb_path_match';

ALTER FUNCTION pg_catalog.jsonb_path_exists SUPPORT jsonb_path_exists_support;
ALTER FUNCTION pg_catalog.jsonb_path_match  SUPPORT jsonb_path_match_support;
