/* 
 * Copyright (C) 2012 Abisource.com
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  
 * 02110-1301 USA.
 */

#include <stdio.h>
#include <time.h>
#include "ODc_util.h"
#include "ut_std_string.h"

std::string ODc_reorderDate(const std::string &dateStr, bool fromISO)
{
    tm dateTm;
    const char* sourceFormat = fromISO ? "%Y-%m-%d" : "%m-%d-%Y";


    memset(&dateTm, 0, sizeof (dateTm));
    UT_strptime(dateStr.c_str(), sourceFormat, &dateTm);

    if (fromISO){
        return UT_std_string_sprintf("%02d-%02d-%d", dateTm.tm_mon,
				     dateTm.tm_mday, dateTm.tm_year + 1900);
    }
    return UT_std_string_sprintf("%d-%02d-%02d", dateTm.tm_year + 1900,
				 dateTm.tm_mon + 1, dateTm.tm_mday);
}

std::string ODc_annotationDate(const std::string &dateStr, bool toISO)
{
    if (dateStr.empty())
        return "";
    int first = 0, second = 0, third = 0;
    char tail[80];
    tail[0] = 0;
    int count = sscanf(dateStr.c_str(), "%d-%d-%d%79s", &first, &second, &third, tail);
    if (count >= 3 && first > 1900)
    {
        if (!toISO)
            return UT_std_string_sprintf("%02d-%02d-%04d", second, third, first);
        if (tail[0] == 'T')
            return dateStr;
        return UT_std_string_sprintf("%04d-%02d-%02dT00:00:00", first, second, third);
    }
    if (count >= 3 && third > 1900)
    {
        if (!toISO)
            return UT_std_string_sprintf("%02d-%02d-%04d", first, second, third);
        return UT_std_string_sprintf("%04d-%02d-%02dT00:00:00", third, first, second);
    }
    return dateStr;
}
