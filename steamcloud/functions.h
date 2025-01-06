#pragma once
#ifndef FUNCTIONS_H
#define FUNCTIONS_H
static int FindPosition(wchar_t* buffer, int startpos = 0, int maxchars = MAX_PATH);

static int FindPosition(wchar_t* buffer, int startpos, int maxchars)
{
	int newpos=-1,start=0;
	if (startpos == 0) start = 0;
	else start = startpos + 1;
	for (int i = start; i < maxchars; i++)
	{
		if (buffer[i] == '\0')
		{
			newpos = i;
			break;
		}
		if (i == maxchars - 1)
		{
			newpos = -1;
			break;
		}
	}
	return newpos;
}

#endif