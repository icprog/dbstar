package com.dbstar.app.media;

import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;

import com.dbstar.app.GDBaseActivity;
import com.dbstar.model.ContentData;
import com.dbstar.model.ContentData.SubTitle;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.util.Log;

public class GDPlayerUtil {

	private static final String TAG = "GDPlayerUtil";
	private static final String Fb0Blank = "/sys/class/graphics/fb0/blank";

	public static void playVideo(Context context, String publicationSetID,
			ContentData content, String mainFile, String drmFile) {
		Log.d(TAG, "file = " + mainFile);
		Log.d(TAG, "drm file = " + drmFile);
		if (mainFile != null && !mainFile.equals("")) {
			Intent intent = new Intent();

			final String schema = "file://";
			String path = schema + mainFile;
			if (drmFile != null && !drmFile.isEmpty()) {
				path += "|" + drmFile;
			}

			Uri uri = Uri.parse(path);

			Log.d(TAG, "play = " + uri.toString());

			intent.setData(uri);
			if (publicationSetID != null && !publicationSetID.isEmpty()) {
				intent.putExtra("publicationset_id", publicationSetID);
			}

			intent.putExtra("publication_id", content.Id);
			intent.putExtra("title", content.Name);
			intent.putExtra("description", content.Description);
			intent.putExtra("director", content.Director);
			intent.putExtra("actors", content.Actors);
			intent.putExtra("type", content.Type);
			intent.putExtra("area", content.Area);
			intent.putExtra("resolution", content.MainFile.Resolution);
			intent.putExtra("bitrate", content.MainFile.BitRate);
			intent.putExtra("codeformat", content.MainFile.CodeFormat);
			
			intent.putExtra("bookmark", content.BookMark);

			if (content.SubTitles != null && content.SubTitles.size() > 0) {
				ArrayList<String> subtitleUris = new ArrayList<String>();
				for (int i = 0; i < content.SubTitles.size(); i++) {
					ContentData.SubTitle subtitle = content.SubTitles.get(i);
					subtitleUris.add(subtitle.URI);
				}
				intent.putStringArrayListExtra("subtitle_uri", subtitleUris);
			}

			intent.setComponent(new ComponentName("com.dbstar.DbstarDVB",
					"com.dbstar.DbstarDVB.VideoPlayer.PlayerMenu"));
			intent.setAction("android.intent.action.View");

			writeSysfs(Fb0Blank, "1"); // hide OSD view
			GDBaseActivity activity = (GDBaseActivity) context;
			activity.startActivity(intent);
		}
	}

	public static int writeSysfs(String path, String val) {
		if (!new File(path).exists()) {
			Log.e(TAG, "File not found: " + path);
			return 1;
		}

		try {
			BufferedWriter writer = new BufferedWriter(new FileWriter(path), 64);
			try {
				writer.write(val);
			} finally {
				writer.close();
			}
			return 0;

		} catch (IOException e) {
			Log.e(TAG, "IO Exception when write: " + path, e);
			return 1;
		}
	}
}
