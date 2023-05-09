#include "postgres.h"

#include "catalog/pg_type_d.h"
#include "catalog/pg_operator_d.h"
#include "lib/stringinfo.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "nodes/supportnodes.h"
#include "parser/parse_func.h"
#include "parser/parse_oper.h"
#include "utils/fmgroids.h"
#include "utils/jsonpath.h"

typedef struct JsonPathContext
{
	StringInfo	buf;
	Jsonb	   *vars;
	bool		varsCantBeSubstituted;
} JsonPathContext;

PG_MODULE_MAGIC;

/*
 * Align StringInfo to int by adding zero padding bytes
 */
static void
alignStringInfoInt(StringInfo buf)
{
	switch (INTALIGN(buf->len) - buf->len)
	{
		case 3:
			appendStringInfoCharMacro(buf, 0);
			/* FALLTHROUGH */
		case 2:
			appendStringInfoCharMacro(buf, 0);
			/* FALLTHROUGH */
		case 1:
			appendStringInfoCharMacro(buf, 0);
			/* FALLTHROUGH */
		default:
			break;
	}
}

/*
 * Reserve space for int32 JsonPathItem pointer.  Now zero pointer is written,
 * actual value will be recorded at '(int32 *) &buf->data[pos]' later.
 */
static int32
reserveSpaceForItemPointer(StringInfo buf)
{
	int32		pos = buf->len;
	int32		ptr = 0;

	appendBinaryStringInfo(buf, (char *) &ptr, sizeof(ptr));

	return pos;
}

static inline int32
appendJsonPathItemHeader(StringInfo buf, JsonPathItemType type)
{
	appendStringInfoChar(buf, (char) type);

	/*
	 * We align buffer to int32 because a series of int32 values often goes
	 * after the header, and we want to read them directly by dereferencing
	 * int32 pointer (see jspInitByBuffer()).
	 */
	alignStringInfoInt(buf);

	/*
	 * Reserve space for next item pointer.  Actual value will be recorded
	 * later, after next and children items processing.
	 */
	return reserveSpaceForItemPointer(buf);
}

static void
flattenJsonPathScalarItem(StringInfo buf, JsonbValue *item)
{
	appendJsonPathItemHeader(buf, (JsonPathItemType) item->type);

	switch (item->type)
	{
		case jbvNull:
			break;
		case jbvString:
			appendBinaryStringInfo(buf, (char *) &item->val.string.len,
								   sizeof(item->val.string.len));
			appendBinaryStringInfo(buf, item->val.string.val,
								   item->val.string.len);
			appendStringInfoChar(buf, '\0');
			break;
		case jbvNumeric:
			appendBinaryStringInfo(buf, (char *) item->val.numeric,
								   VARSIZE(item->val.numeric));
			break;
		case jbvBool:
			appendBinaryStringInfo(buf, (char *) &item->val.boolean,
								   sizeof(item->val.boolean));
			break;
		default:
			elog(ERROR, "invalid scalar jsonb value type: %d", item->type);
	}
}

static bool
replaceVariableReference(JsonPathContext *cxt, JsonPathItem *var, int32 pos)
{
	JsonbValue	name;
	JsonbValue *value;

	name.type = jbvString;
	name.val.string.val = jspGetString(var, &name.val.string.len);

	value = findJsonbValueFromContainer(&cxt->vars->root, JB_FOBJECT, &name);

	if (!value)
		return false;

	cxt->buf->len = pos + JSONPATH_HDRSZ;	/* reset buffer */

	if (!IsAJsonbScalar(value))
	{
		cxt->varsCantBeSubstituted = true;
		return false;
	}

	flattenJsonPathScalarItem(cxt->buf, value);

	return true;
}

static int32
copyJsonPathItem(JsonPathContext *cxt, JsonPathItem *item,
				 int32 *pLastOffset, int32 *pNextOffset)
{
	StringInfo	buf = cxt->buf;
	int32		pos = buf->len - JSONPATH_HDRSZ;
	JsonPathItem next;
	int32		offs = 0;
	int32		nextOffs;

	check_stack_depth();

	nextOffs = appendJsonPathItemHeader(buf, item->type);

	switch (item->type)
	{
		case jpiNull:
		case jpiCurrent:
		case jpiAnyArray:
		case jpiAnyKey:
		case jpiType:
		case jpiSize:
		case jpiAbs:
		case jpiFloor:
		case jpiCeiling:
		case jpiDouble:
		case jpiKeyValue:
		case jpiLast:
			break;

		case jpiRoot:
			break;

		case jpiKey:
		case jpiString:
		case jpiVariable:
			{
				int32		len;
				char	   *data = jspGetString(item, &len);

				if (item->type == jpiVariable && cxt->vars &&
					replaceVariableReference(cxt, item, pos))
					break;

				appendBinaryStringInfo(buf, (const char *) &len, sizeof(len));
				appendBinaryStringInfo(buf, data, len);
				appendStringInfoChar(buf, '\0');
				break;
			}

		case jpiNumeric:
			{
				Numeric		num = jspGetNumeric(item);

				appendBinaryStringInfo(buf, (char *) num, VARSIZE(num));
				break;
			}

		case jpiBool:
			appendStringInfoChar(buf, jspGetBool(item) ? 1 : 0);
			break;

		case jpiFilter:
		case jpiNot:
		case jpiExists:
		case jpiIsUnknown:
		case jpiPlus:
		case jpiMinus:
			{
				JsonPathItem arg;
				int32		argoffs;
				int32		argpos;

				argoffs = buf->len;
				appendBinaryStringInfo(buf, (const char *) &offs, sizeof(offs));

				if (!item->content.arg)
					break;

				jspGetArg(item, &arg);
				argpos = copyJsonPathItem(cxt, &arg, NULL, NULL);
				*(int32 *) &buf->data[argoffs] = argpos - pos;
				break;
			}

		case jpiAnd:
		case jpiOr:
		case jpiAdd:
		case jpiSub:
		case jpiMul:
		case jpiDiv:
		case jpiMod:
		case jpiEqual:
		case jpiNotEqual:
		case jpiLess:
		case jpiGreater:
		case jpiLessOrEqual:
		case jpiGreaterOrEqual:
		case jpiStartsWith:
			{
				JsonPathItem larg;
				JsonPathItem rarg;
				int32		loffs;
				int32		roffs;
				int32		lpos;
				int32		rpos;

				loffs = buf->len;
				appendBinaryStringInfo(buf, (const char *) &offs, sizeof(offs));

				roffs = buf->len;
				appendBinaryStringInfo(buf, (const char *) &offs, sizeof(offs));

				jspGetLeftArg(item, &larg);
				lpos = copyJsonPathItem(cxt, &larg, NULL, NULL);
				*(int32 *) &buf->data[loffs] = lpos - pos;

				jspGetRightArg(item, &rarg);
				rpos = copyJsonPathItem(cxt, &rarg, NULL, NULL);
				*(int32 *) &buf->data[roffs] = rpos - pos;

				break;
			}

		case jpiLikeRegex:
			{
				JsonPathItem expr;
				int32		eoffs;
				int32		epos;

				appendBinaryStringInfo(buf,
									(char *) &item->content.like_regex.flags,
									sizeof(item->content.like_regex.flags));

				eoffs = buf->len;
				appendBinaryStringInfo(buf, (char *) &offs /* fake value */, sizeof(offs));

				appendBinaryStringInfo(buf,
									(char *) &item->content.like_regex.patternlen,
									sizeof(item->content.like_regex.patternlen));
				appendBinaryStringInfo(buf, item->content.like_regex.pattern,
									   item->content.like_regex.patternlen);
				appendStringInfoChar(buf, '\0');

				jspInitByBuffer(&expr, item->base, item->content.like_regex.expr);
				epos = copyJsonPathItem(cxt, &expr, NULL, NULL);
				*(int32 *) &buf->data[eoffs] = epos - pos;
			}
			break;

		case jpiIndexArray:
			{
				int32		nelems = item->content.array.nelems;
				int32		i;
				int			offset;

				appendBinaryStringInfo(buf, (char *) &nelems, sizeof(nelems));
				offset = buf->len;
				appendStringInfoSpaces(buf, sizeof(int32) * 2 * nelems);

				for (i = 0; i < nelems; i++, offset += 2 * sizeof(int32))
				{
					JsonPathItem from;
					JsonPathItem to;
					int32	   *ppos;
					int32		frompos;
					int32		topos;
					bool		range;

					range = jspGetArraySubscript(item, &from, &to, i);

					frompos = copyJsonPathItem(cxt, &from, NULL, NULL) - pos;

					if (range)
						topos = copyJsonPathItem(cxt, &to, NULL, NULL) - pos;
					else
						topos = 0;

					ppos = (int32 *) &buf->data[offset];
					ppos[0] = frompos;
					ppos[1] = topos;
				}
			}
			break;

		case jpiAny:
			appendBinaryStringInfo(buf, (char *) &item->content.anybounds.first,
								   sizeof(item->content.anybounds.first));
			appendBinaryStringInfo(buf, (char *) &item->content.anybounds.last,
								   sizeof(item->content.anybounds.last));
			break;

		default:
			elog(ERROR, "Unknown jsonpath item type: %d", item->type);
	}

	if (jspGetNext(item, &next))
	{
		int32		nextPos = copyJsonPathItem(cxt, &next,
											   pLastOffset, pNextOffset);

		*(int32 *) &buf->data[nextOffs] = nextPos - pos;
	}
	else if (pLastOffset)
	{
		*pLastOffset = pos;
		*pNextOffset = nextOffs;
	}

	return pos;
}

static JsonPath *
copyJsonPath(JsonPath *jsp, Jsonb *vars)
{
	JsonPath   *res;
	JsonPathContext cxt;
	JsonPathItem root;
	StringInfoData buf;
	bool		lax = !!(jsp->header & JSONPATH_LAX);
	int			sizeEstimation = VARSIZE_ANY(jsp) + (vars ? VARSIZE_ANY(vars) : 0);

	cxt.buf = &buf;
	cxt.vars = vars;
	cxt.varsCantBeSubstituted = false;

	jspInit(&root, jsp);

	initStringInfo(&buf);
	enlargeStringInfo(&buf, sizeEstimation);

	appendStringInfoSpaces(&buf, JSONPATH_HDRSZ);
	/* alignStringInfoInt(cxt->buf); */

	copyJsonPathItem(&cxt, &root, 0, false);

	if (cxt.varsCantBeSubstituted)
		return NULL;

	res = (JsonPath *) buf.data;
	SET_VARSIZE(res, buf.len);
	res->header = JSONPATH_VERSION;
	if (lax)
		res->header |= JSONPATH_LAX;

	return res;
}

static JsonPath *
substituteVariables(JsonPath *jsp, Jsonb *vars)
{
	return copyJsonPath(jsp, vars);
}

PG_FUNCTION_INFO_V1(jsonpath_embed_vars);

Datum
jsonpath_embed_vars(PG_FUNCTION_ARGS)
{
	JsonPath   *jsp = PG_GETARG_JSONPATH_P(0);
	Jsonb	   *vars = PG_GETARG_JSONB_P(1);

	if (!(jsp = substituteVariables(jsp, vars)))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("cannot embed non-scalar jsonpath variables")));

	PG_RETURN_JSONPATH_P(jsp);
}

static Const *
getConstExpr(Expr *expr, Oid typid)
{
	if (!IsA(expr, Const) ||
		((Const *) expr)->constisnull ||
		((Const *) expr)->consttype != typid)
		return NULL;

	return (Const *) expr;
}

/* Planner support for jsonb_path_match() and jsonb_path_exists() */
static Node *
jsonb_path_support(Node *rawreq, bool exists)
{
	Node       *ret = NULL;

	if (IsA(rawreq, SupportRequestIndexCondition))
	{
		/* Try to convert operator/function call to index conditions */
		SupportRequestIndexCondition *req = (SupportRequestIndexCondition *) rawreq;

		/*
		 * Currently we have no "reverse" match operators with the pattern on
		 * the left, so we only need consider cases with the indexkey on the
		 * left.
		 */
		if (req->indexarg != 0)
			return NULL;

		if (is_funcclause(req->node))
		{
			FuncExpr   *clause = (FuncExpr *) req->node;
			Expr	   *opexpr;
			Expr	   *jspexpr;
			Expr	   *jsonexpr;
			Const	   *pathexpr;
			Const	   *varsexpr;
			Const	   *silentexpr;
			Jsonb	   *vars;
			Oid			oproid;

			if (list_length(clause->args) < 4)
				return NULL;

			if (!(pathexpr = getConstExpr(lsecond(clause->args), JSONPATHOID)))
				return NULL;

			if (!(silentexpr = getConstExpr(lfourth(clause->args), BOOLOID)) ||
				!DatumGetBool(silentexpr->constvalue))
				return NULL;

			if ((varsexpr = getConstExpr(lthird(clause->args), JSONBOID)))
			{
				vars = DatumGetJsonbP(varsexpr->constvalue);

				if (!JsonContainerIsObject(&vars->root))
					return NULL;

				if (JsonContainerSize(&vars->root) <= 0)
					jspexpr = (Expr *) pathexpr;
				else
				{
					JsonPath   *jsp = DatumGetJsonPathP(pathexpr->constvalue);

					jsp = substituteVariables(jsp, vars);

					if (!jsp)
						return NULL;

					jspexpr = (Expr *) makeConst(JSONPATHOID, -1, InvalidOid,
												 -1, PointerGetDatum(jsp),
												 false, false);
				}
			}
			else
			{
				List	   *args = list_make2(pathexpr, lthird(clause->args));
				Oid			argoids[] = { JSONPATHOID, JSONBOID };
				List	   *funcname = list_make1(makeString("jsonpath_embed_vars"));
				Oid			jsonpath_embed_vars_oid = LookupFuncName(funcname, 2, argoids, true);

				if (!OidIsValid(jsonpath_embed_vars_oid))
					return NULL;

				jspexpr = (Expr *) makeFuncExpr(jsonpath_embed_vars_oid,
												JSONPATHOID, args,
												InvalidOid, InvalidOid,
												COERCE_EXPLICIT_CALL);
			}

			jsonexpr = linitial(clause->args);

			/* oproid = exists ? JsonbPathExistsOperator : JsonbPathMatchOperator; */
			oproid = LookupOperName(NULL, list_make2(makeString("pg_catalog"),
													 makeString(exists ? "@?" : "@@")),
									JSONBOID, JSONPATHOID, true, -1);

			if (!OidIsValid(oproid))
				return NULL;

			opexpr = make_opclause(oproid, BOOLOID, false,
								   jsonexpr, jspexpr,
								   InvalidOid, req->indexcollation);

			req->lossy = false;

			return (Node *) list_make1(opexpr);
		}
	}

	return ret;
}

PG_FUNCTION_INFO_V1(jsonb_path_match_support);

/* Planner support for jsonb_path_match() */
Datum
jsonb_path_match_support(PG_FUNCTION_ARGS)
{
	Node       *rawreq = (Node *) PG_GETARG_POINTER(0);

	PG_RETURN_POINTER(jsonb_path_support(rawreq, false));
}

PG_FUNCTION_INFO_V1(jsonb_path_exists_support);

/* Planner support for jsonb_path_exists() */
Datum
jsonb_path_exists_support(PG_FUNCTION_ARGS)
{
	Node       *rawreq = (Node *) PG_GETARG_POINTER(0);

	PG_RETURN_POINTER(jsonb_path_support(rawreq, true));
}
