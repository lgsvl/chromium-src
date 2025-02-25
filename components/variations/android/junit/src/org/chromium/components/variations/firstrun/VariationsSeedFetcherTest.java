// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.components.variations.firstrun;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import android.content.SharedPreferences;
import android.os.Looper;
import android.util.Base64;

import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RuntimeEnvironment;
import org.robolectric.annotation.Config;

import org.chromium.base.ContextUtils;
import org.chromium.base.ThreadUtils;
import org.chromium.testing.local.LocalRobolectricTestRunner;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.net.HttpURLConnection;

/**
 * Tests for VariationsSeedFetcher
 */
@RunWith(LocalRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class VariationsSeedFetcherTest {
    private HttpURLConnection mConnection;
    private VariationsSeedFetcher mFetcher;
    private SharedPreferences mPrefs;

    @Before
    public void setUp() throws IOException {
        ContextUtils.initApplicationContextForTests(RuntimeEnvironment.application);
        // Pretend we are not on the UI thread, since the class we are testing is supposed to run
        // only on a background thread.
        ThreadUtils.setUiThread(mock(Looper.class));
        mFetcher = spy(VariationsSeedFetcher.get());
        mConnection = mock(HttpURLConnection.class);
        doReturn(mConnection)
                .when(mFetcher)
                .getServerConnection(VariationsSeedFetcher.VariationsPlatform.ANDROID, "");
        mPrefs = ContextUtils.getAppSharedPreferences();
        mPrefs.edit().clear().apply();
    }

    @After
    public void tearDown() {
        ThreadUtils.setUiThread(null);
    }

    /**
     * Test method for {@link VariationsSeedFetcher#fetchSeed()}.
     *
     * @throws IOException
     */
    @Test
    public void testFetchSeed() throws IOException {
        // Pretend we are on a background thread; set the UI thread looper to something other than
        // the current thread.

        when(mConnection.getResponseCode()).thenReturn(HttpURLConnection.HTTP_OK);
        when(mConnection.getHeaderField("X-Seed-Signature")).thenReturn("signature");
        when(mConnection.getHeaderField("X-Country")).thenReturn("Nowhere Land");
        when(mConnection.getHeaderField("Date")).thenReturn("A date");
        when(mConnection.getHeaderField("IM")).thenReturn("gzip");
        when(mConnection.getInputStream()).thenReturn(new ByteArrayInputStream("1234".getBytes()));

        mFetcher.fetchSeed("");

        assertThat(mPrefs.getString(VariationsSeedBridge.VARIATIONS_FIRST_RUN_SEED_SIGNATURE, ""),
                equalTo("signature"));
        assertThat(mPrefs.getString(VariationsSeedBridge.VARIATIONS_FIRST_RUN_SEED_COUNTRY, ""),
                equalTo("Nowhere Land"));
        assertThat(mPrefs.getString(VariationsSeedBridge.VARIATIONS_FIRST_RUN_SEED_DATE, ""),
                equalTo("A date"));
        assertTrue(mPrefs.getBoolean(
                VariationsSeedBridge.VARIATIONS_FIRST_RUN_SEED_IS_GZIP_COMPRESSED, false));
        assertThat(mPrefs.getString(VariationsSeedBridge.VARIATIONS_FIRST_RUN_SEED_BASE64, ""),
                equalTo(Base64.encodeToString("1234".getBytes(), Base64.NO_WRAP)));
    }

    /**
    * Test method for {@link VariationsSeedFetcher#fetchSeed()} when no fetch is needed
    */
    @Test
    public void testFetchSeed_noFetchNeeded() throws IOException {
        mPrefs.edit().putBoolean(VariationsSeedFetcher.VARIATIONS_INITIALIZED_PREF, true).apply();

        mFetcher.fetchSeed("");

        verify(mConnection, never()).connect();
    }

    /**
    * Test method for {@link VariationsSeedFetcher#fetchSeed()} with a bad response
    */
    @Test
    public void testFetchSeed_badResponse() throws IOException {
        when(mConnection.getResponseCode()).thenReturn(HttpURLConnection.HTTP_NOT_FOUND);

        mFetcher.fetchSeed("");

        assertTrue(mPrefs.getBoolean(VariationsSeedFetcher.VARIATIONS_INITIALIZED_PREF, false));
        assertFalse(VariationsSeedBridge.hasJavaPref());
    }

    /**
    * Test method for {@link VariationsSeedFetcher#fetchSeed()} with an exception when connecting
    */
    @Test
    public void testFetchSeed_IOException() throws IOException {
        doThrow(new IOException()).when(mConnection).connect();

        mFetcher.fetchSeed("");

        assertTrue(mPrefs.getBoolean(VariationsSeedFetcher.VARIATIONS_INITIALIZED_PREF, false));
        assertFalse(VariationsSeedBridge.hasJavaPref());
    }
}
