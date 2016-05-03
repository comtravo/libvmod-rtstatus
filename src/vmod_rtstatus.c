#include <inttypes.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <time.h>

#include "vmod_rtstatus.h"
#include "vapi/vsc.h"
#include "vrt.h"
#include "vtim.h"
#include "vcl.h"
//static struct hitrate hitrate;
//static struct load load;


/*int
event_function(const struct vrt_ctx *ctx, struct vmod_priv *priv,
    enum vcl_event_e e)
{
	memset(&hitrate, 0, sizeof(struct hitrate));
	memset(&load, 0, sizeof(struct load));
	beresp_hdr = beresp_body = 0;
	bereq_hdr = bereq_body = 0;
	be_happy = 0;
	n_be = 0;
	cont = 0;
	return (0);
}*/

static void
update_counter(struct counter *counter, double val)
{
        if (counter->n < counter->nmax)
                counter->n++;
        counter->acc += (val - counter->acc) / (double)counter->n;
}

void
rate(struct rtstatus_priv *rtstatus, struct VSM_data *vd)
{
	double hr, mr, ratio, tv, dt, reqload;
	struct VSC_C_main *VSC_C_main;
	uint64_t hit, miss;
	time_t up;
	int req;

	VSC_C_main = VSC_Main(vd, NULL);
        if (VSC_C_main == NULL)
                return;

        tv = VTIM_mono();
        dt = tv - hitrate.tm;
        hitrate.tm = tv;

        hit = VSC_C_main->cache_hit;
        miss = VSC_C_main->cache_miss;
        hr = (hit - hitrate.hit) / dt;
        mr = (miss - hitrate.miss) / dt;
        hitrate.hit = hit;
        hitrate.miss = miss;

        if (hr + mr != 0)
                ratio = hr / (hr + mr);
        else
                ratio = 0;

	up = VSC_C_main->uptime;
	req = VSC_C_main->client_req;
	reqload  =  ((req - load.req) / dt);
	load.req = req;

	update_counter(&hitrate.hr, ratio);
	update_counter(&load.rl, reqload);

	VSB_printf(rtstatus->vsb, "\t\"uptime\" : \"%d+%02d:%02d:%02d\",\n",
	    (int)up / 86400, (int)(up % 86400) / 3600,
	    (int)(up % 3600) / 60, (int)up % 60);
	VSB_printf(rtstatus->vsb, "\t\"uptime_sec\": %.2f,\n", (double)up);
	VSB_printf(rtstatus->vsb, "\t\"hitrate\": %.2f,\n", hitrate.hr.acc * 100);
	VSB_printf(rtstatus->vsb, "\t\"load\": %.2f,\n", load.rl.acc);
	VSB_printf(rtstatus->vsb, "\t\"delta\": %.2f,\n", rtstatus->delta);
}

int
json_status(void *priv, const struct VSC_point *const pt)
{
	struct rtstatus_priv *rtstatus = priv;
	const struct VSC_section *sec;
	uint64_t val;

	if (pt == NULL)
		return (0);

	val = *(const volatile uint64_t *)pt->ptr;
	sec = pt->section;

	if (rtstatus->jp)
		rtstatus->jp = 0;
	else
		VSB_cat(rtstatus->vsb, ",\n");
		VSB_cat(rtstatus->vsb, "\t\"");
	if (strcmp(sec->fantom->type, "")) {
		VSB_cat(rtstatus->vsb, sec->fantom->type);
		VSB_cat(rtstatus->vsb, ".");
	}
	if (strcmp(sec->fantom->ident, "")) {
		VSB_cat(rtstatus->vsb, sec->fantom->ident);
		VSB_cat(rtstatus->vsb, ".");
	}
	VSB_cat(rtstatus->vsb, pt->desc->name);
	VSB_cat(rtstatus->vsb, "\": {");
	if (strcmp(sec->fantom->type, "")) {
		VSB_cat(rtstatus->vsb, "\"type\": \"");
		VSB_cat(rtstatus->vsb, sec->fantom->type);
		VSB_cat(rtstatus->vsb, "\", ");
	}
	if (strcmp(sec->fantom->ident, "")) {
		VSB_cat(rtstatus->vsb, "\"ident\": \"");
		VSB_cat(rtstatus->vsb, sec->fantom->ident);
		VSB_cat(rtstatus->vsb, "\", ");
	}
	VSB_cat(rtstatus->vsb, "\"descr\": \"");
	VSB_cat(rtstatus->vsb, pt->desc->sdesc);
	VSB_cat(rtstatus->vsb, "\", ");
	VSB_printf(rtstatus->vsb,"\"value\": %" PRIu64 "}", val);
	if (rtstatus->jp)
		VSB_cat(rtstatus->vsb, "\n");
	return(0);
}

int
creepy_math(void *priv, const struct VSC_point *const pt)
{
	struct rtstatus_priv *rtstatus = priv;
	const struct VSC_section *sec;
	uint64_t val;

	if (pt == NULL)
		return (0);

	val = *(const volatile uint64_t *)pt->ptr;
	sec = pt->section;
	if(!strcmp(sec->fantom->type,"MAIN")){
		if(!strcmp(pt->desc->name, "n_backend")){
			n_be = (int)val;
		}
	}
	if (!strcmp(sec->fantom->type, "VBE")) {
	if(!strcmp(pt->desc->name, "happy")) {
		be_happy = val;
	}
		if(!strcmp(pt->desc->name, "bereq_hdrbytes"))
			bereq_hdr = val;
		if(!strcmp(pt->desc->name, "bereq_bodybytes")) {
			bereq_body = val;
			VSB_cat(rtstatus->vsb, "{\"server_name\":\"");
			VSB_cat(rtstatus->vsb, pt->section->fantom->ident);
			VSB_printf(rtstatus->vsb,"\", \"happy\": %" PRIu64,
			    be_happy);
			VSB_printf(rtstatus->vsb,", \"bereq_tot\": %" PRIu64 ",",
			    bereq_body + bereq_hdr);
		}

		if(!strcmp(pt->desc->name, "beresp_hdrbytes"))
			beresp_hdr = val;
		if(!strcmp(pt->desc->name, "beresp_bodybytes")) {
			beresp_body = val;
			VSB_printf(rtstatus->vsb,"\"beresp_tot\": %" PRIu64 "}",
			    beresp_body + beresp_hdr);
			if(cont < (n_be -1)) {
				VSB_cat(rtstatus->vsb, ",\n\t\t");
				cont++;
			}
		}
	}
	return(0);
}

int
run_subroutine(struct rtstatus_priv *rtstatus, struct VSM_data *vd)
{
	hitrate.hr.nmax = rtstatus->delta;
	load.rl.nmax = rtstatus->delta;
	VSB_cat(rtstatus->vsb, "{\n");
	rate(rtstatus, vd);
	general_info(rtstatus);
	VSB_cat(rtstatus->vsb, "\t\"be_info\": [");
	(void)VSC_Iter(vd, NULL, creepy_math, rtstatus);
	VSB_cat(rtstatus->vsb, "],\n");
	cont = 0;
	(void)VSC_Iter(vd, NULL, json_status, rtstatus);
	VSB_cat(rtstatus->vsb, "\n}\n");
	return(0);
}

