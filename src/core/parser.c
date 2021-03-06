#include <lip/core/parser.h>
#include <lip/core/array.h>
#include "arena_allocator.h"
#include "utils.h"

static lip_stream_status_t
lip_parser_peek_token(lip_parser_t* parser, lip_token_t* token)
{
	if(parser->buffered)
	{
		*token = parser->token;
		return parser->lexer_status;
	}
	else
	{
		parser->lexer_status = lip_lexer_next_token(&parser->lexer, &parser->token);
		*token = parser->token;
		parser->buffered = true;
		return parser->lexer_status;
	}
}

static lip_stream_status_t
lip_parser_next_token(lip_parser_t* parser, lip_token_t* token)
{
	if(parser->buffered)
	{
		parser->buffered = false;
		*token = parser->token;
		return parser->lexer_status;
	}
	else
	{
		parser->lexer_status = lip_lexer_next_token(&parser->lexer, &parser->token);
		*token = parser->token;
		return parser->lexer_status;
	}
}

static lip_stream_status_t
lip_parser_parse_list(lip_parser_t* parser, lip_token_t* token, lip_sexp_t* sexp)
{
	sexp->location.start = token->location.start;
	sexp->type = LIP_SEXP_LIST;
	lip_array(lip_sexp_t) list =
		lip_array_create(parser->arena_allocator, lip_sexp_t, 16);
	lip_sexp_t element;

	while(true)
	{
		lip_token_t next_token;
		lip_stream_status_t peek = lip_parser_peek_token(parser, &next_token);
		if(peek == LIP_STREAM_OK && next_token.type == LIP_TOKEN_RPAREN)
		{
			lip_parser_next_token(parser, &next_token);
			sexp->location.end = next_token.location.end;
			sexp->data.list = list;
			return LIP_STREAM_OK;
		}

		lip_stream_status_t status = lip_parser_next_sexp(parser, &element);
		switch(status)
		{
			case LIP_STREAM_OK:
				lip_array_push(list, element);
				break;
			case LIP_STREAM_ERROR:
				lip_array_destroy(list);
				return LIP_STREAM_ERROR;
			case LIP_STREAM_END:
				lip_array_destroy(list);
				lip_set_last_error(
					&parser->last_error,
					LIP_PARSE_UNTERMINATED_LIST,
					token->location,
					NULL
				);
				return LIP_STREAM_ERROR;
		}
	}
}

static lip_stream_status_t
lip_parser_parse_element(lip_parser_t* parser, lip_token_t* token, lip_sexp_t* sexp)
{
	(void)parser;
	switch(token->type)
	{
	case LIP_TOKEN_STRING:
		sexp->type = LIP_SEXP_STRING;
		break;
	case LIP_TOKEN_SYMBOL:
		sexp->type = LIP_SEXP_SYMBOL;
		break;
	case LIP_TOKEN_NUMBER:
		sexp->type = LIP_SEXP_NUMBER;
		break;
	default:
		// Impossibru!!
		lip_set_last_error(
			&parser->last_error,
			LIP_PARSE_UNEXPECTED_TOKEN,
			token->location,
			NULL
		);
		return LIP_STREAM_ERROR;
	}

	sexp->location = token->location;
	sexp->data.string = token->lexeme;
	return LIP_STREAM_OK;;
}

static lip_stream_status_t
lip_parser_parse(lip_parser_t* parser, lip_token_t* token, lip_sexp_t* sexp)
{
	switch(token->type)
	{
		case LIP_TOKEN_LPAREN:
			return lip_parser_parse_list(parser, token, sexp);
		case LIP_TOKEN_RPAREN:
			lip_set_last_error(
				&parser->last_error,
				LIP_PARSE_UNEXPECTED_TOKEN,
				token->location,
				&parser->token
			);
			return LIP_STREAM_ERROR;
		case LIP_TOKEN_STRING:
		case LIP_TOKEN_SYMBOL:
		case LIP_TOKEN_NUMBER:
			return lip_parser_parse_element(parser, token, sexp);
		case LIP_TOKEN_QUOTE:
		case LIP_TOKEN_QUASIQUOTE:
		case LIP_TOKEN_UNQUOTE:
		case LIP_TOKEN_UNQUOTE_SPLICING:
			{
				lip_sexp_t quoted_sexp;
				lip_stream_status_t status;
				switch(status = lip_parser_next_sexp(parser, &quoted_sexp))
				{
					case LIP_STREAM_OK:
						{
							lip_array(lip_sexp_t) list = lip_array_create(
								parser->arena_allocator, lip_sexp_t, 16
							);

							const char* symbol;
							switch(token->type)
							{
								case LIP_TOKEN_QUOTE:
									symbol = "quote";
									break;
								case LIP_TOKEN_QUASIQUOTE:
									symbol = "quasiquote";
									break;
								case LIP_TOKEN_UNQUOTE:
									symbol = "unquote";
									break;
								case LIP_TOKEN_UNQUOTE_SPLICING:
									symbol = "unquote-splicing";
									break;
								default:
									// Impossibru!!
									lip_set_last_error(
										&parser->last_error,
										LIP_PARSE_UNEXPECTED_TOKEN,
										token->location,
										NULL
									);
									return LIP_STREAM_ERROR;
							}

							lip_sexp_t quote_sexp = {
								.type = LIP_SEXP_SYMBOL,
								.location = token->location,
								.data  = { .string = lip_string_ref(symbol) }
							};

							lip_array_push(list, quote_sexp);
							lip_array_push(list, quoted_sexp);

							*sexp = (lip_sexp_t){
								.type = LIP_SEXP_LIST,
								.location = {
									.start = token->location.start,
									.end = quoted_sexp.location.end
								},
								.data = { .list = list }
							};
						}
						return LIP_STREAM_OK;
					case LIP_STREAM_END:
						lip_set_last_error(
							&parser->last_error,
							LIP_PARSE_UNEXPECTED_TOKEN,
							token->location,
							&parser->token
						);
						return LIP_STREAM_ERROR;
					case LIP_STREAM_ERROR:
						return LIP_STREAM_ERROR;
				}
			}
	}

	// Impossibru!!
	return LIP_STREAM_ERROR;
}

void
lip_parser_init(lip_parser_t* parser, lip_allocator_t* allocator)
{
	parser->allocator = allocator;
	parser->arena_allocator =
		lip_arena_allocator_create(allocator, sizeof(lip_sexp_t) * 64, true);
	lip_lexer_init(&parser->lexer, allocator);
	lip_parser_reset(parser, NULL);
}

void
lip_parser_cleanup(lip_parser_t* parser)
{
	lip_parser_reset(parser, NULL);
	lip_arena_allocator_destroy(parser->arena_allocator);
	lip_lexer_cleanup(&parser->lexer);
}

void
lip_parser_reset(lip_parser_t* parser, lip_in_t* input)
{
	lip_clear_last_error(&parser->last_error);
	lip_arena_allocator_reset(parser->arena_allocator);
	lip_lexer_reset(&parser->lexer, input);
	parser->buffered = false;
}

lip_stream_status_t
lip_parser_next_sexp(lip_parser_t* parser, lip_sexp_t* sexp)
{
	lip_token_t token;
	lip_clear_last_error(&parser->last_error);

	lip_stream_status_t status = lip_parser_next_token(parser, &token);
	switch(status)
	{
		case LIP_STREAM_OK:
			return lip_parser_parse(parser, &token, sexp);
		case LIP_STREAM_ERROR:
			lip_set_last_error(
				&parser->last_error,
				LIP_PARSE_LEX_ERROR,
				lip_lexer_last_error(&parser->lexer)->location,
				lip_lexer_last_error(&parser->lexer)
			);
			return LIP_STREAM_ERROR;
		case LIP_STREAM_END:
			return LIP_STREAM_END;
	}

	// Impossibru!!
	return LIP_STREAM_ERROR;
}

const lip_error_t*
lip_parser_last_error(lip_parser_t* parser)
{
	return lip_last_error(&parser->last_error);
}
