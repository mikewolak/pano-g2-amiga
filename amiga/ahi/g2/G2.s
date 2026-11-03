| Copyright (c) 2026 Mike Wolak, EPROM Foundry Inc., for the parts written for the G2 Toccata driver.
| Built on the AHI 6.0 driver sources (Martin Blom); those parts keep the terms of COPYING.DRIVERS in this directory.
	FORM_START	AHIM

	CHUNK_START	AUDN
	.asciz		"g2"
	CHUNK_END

	CHUNK_START	AUDM
1:
	LONG2		AHIDB_AudioID,	0x00470001
	LONG2		AHIDB_Volume,	TRUE
	LONG2		AHIDB_Panning,	TRUE
	LONG2		AHIDB_Stereo,	TRUE
	LONG2		AHIDB_HiFi,	FALSE
	LONG2		AHIDB_MultTable,FALSE
	LONG2		AHIDB_Name,	2f-1b
	LONG		TAG_DONE
2:
	.asciz		"G2 Toccata:16 bit stereo++"
	CHUNK_END

	CHUNK_START	AUDM
1:
	LONG2		AHIDB_AudioID,	0x00470002
	LONG2		AHIDB_Volume,	TRUE
	LONG2		AHIDB_Panning,	TRUE
	LONG2		AHIDB_Stereo,	TRUE
	LONG2		AHIDB_HiFi,	TRUE
	LONG2		AHIDB_MultTable,FALSE
	LONG2		AHIDB_Name,	2f-1b
	LONG		TAG_DONE
2:
	.asciz		"G2 Toccata:HiFi 16 bit stereo++"
	CHUNK_END

	CHUNK_START	AUDM
1:
	LONG2		AHIDB_AudioID,	0x00470003
	LONG2		AHIDB_Volume,	TRUE
	LONG2		AHIDB_Panning,	TRUE
	LONG2		AHIDB_Stereo,	FALSE
	LONG2		AHIDB_HiFi,	FALSE
	LONG2		AHIDB_MultTable,FALSE
	LONG2		AHIDB_Name,	2f-1b
	LONG		TAG_DONE
2:
	.asciz		"G2 Toccata:16 bit mono"
	CHUNK_END

	CHUNK_START	AUDM
1:
	LONG2		AHIDB_AudioID,	0x00470004
	LONG2		AHIDB_Volume,	TRUE
	LONG2		AHIDB_Panning,	TRUE
	LONG2		AHIDB_Stereo,	FALSE
	LONG2		AHIDB_HiFi,	TRUE
	LONG2		AHIDB_MultTable,FALSE
	LONG2		AHIDB_Name,	2f-1b
	LONG		TAG_DONE
2:
	.asciz		"G2 Toccata:HiFi 16 bit mono"
	CHUNK_END

	FORM_END

	.END
