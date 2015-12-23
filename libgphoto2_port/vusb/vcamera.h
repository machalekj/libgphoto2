/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* vcamera.h
 *
 * Copyright (c) 2015 Marcus Meissner <marcus@jet.franken.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, 
 * but WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details. 
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#ifndef __VCAMERA_H__
#define __VCAMERA_H__

typedef struct ptpcontainer {
	unsigned int size;
	unsigned int type;
	unsigned int code;
	unsigned int seqnr;
	unsigned int nparams;
	unsigned int params[6];
} ptpcontainer;

typedef struct vcamera {
	int (*init)(struct vcamera*);
	int (*exit)(struct vcamera*);
	int (*open)(struct vcamera*);
	int (*close)(struct vcamera*);

	int (*read)(struct vcamera*,  int ep, char *data, int bytes);
	int (*readint)(struct vcamera*,  char *data, int bytes, int timeout);
	int (*write)(struct vcamera*, int ep, const char *data, int bytes);

	unsigned char	*inbulk;
	int		nrinbulk;
	unsigned char	*outbulk;
	int		nroutbulk;

	unsigned int	seqnr;

	unsigned int	session;
	ptpcontainer	ptpcmd;
} vcamera;

vcamera *vcamera_new(void);

#endif